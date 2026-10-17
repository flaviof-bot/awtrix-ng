// Effects on tall panels (PR 1's 6e81d91 and 4f0137d, re-ported onto 1.2.2's effects). 1.2.2
// already sizes the swirls and the simulated games to the canvas; these cases pin that at every
// height from 8 to 16 instead of PR 1's replayed games.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>
#include <unity.h>
#include "core/effects/effects/MoreEffects.h"
#include "core/effects/effects/SimulatedEffects.h"

using namespace awtrix;
void setUp() {}
void tearDown() {}

static int lit(const Canvas& c) {
  int n = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) n += c.getPixel(x, y) != 0;
  return n;
}

// SwirlIn and SwirlOut paint every pixel of any canvas and drift in opposite directions.
static void test_swirls_span_and_travel() {
  for (int h = 8; h <= 16; ++h)
    for (int w : {32, 53}) {
      Canvas in(w, h), out(w, h), later(w, h);
      SwirlInEffect swirlIn;
      SwirlOutEffect swirlOut;
      for (int64_t f : {0LL, 12LL, 300LL}) {
        swirlIn.render(in, f);
        swirlOut.render(out, -f);
        TEST_ASSERT_EQUAL_INT(w * h, lit(in));
        TEST_ASSERT_EQUAL_UINT32_ARRAY(in.data(), out.data(), in.size());
      }
      swirlIn.render(in, 0);
      swirlIn.render(later, 12);
      TEST_ASSERT_FALSE(std::equal(in.data(), in.data() + in.size(), later.data()));
    }
}

// Two fresh instances fed the same frames draw the same pictures, and nothing is written outside
// the frame buffer, at widths 4 to 80 and every height from 8 to 16.
static void test_determinism_and_no_writes_outside_canvas() {
  for (int w : {4, 32, 53, 64, 80})
    for (int h = 8; h <= 16; ++h) {
      BrickBreakerEffect brickA, brickB;
      SnakeEffect snakeA, snakeB;
      PingPongEffect pongA, pongB;
      SwirlInEffect inA, inB;
      SwirlOutEffect outA, outB;
      IEffect* first[] = {&brickA, &snakeA, &pongA, &inA, &outA};
      IEffect* second[] = {&brickB, &snakeB, &pongB, &inB, &outB};
      std::vector<uint32_t> storage(w * h + 2, 0xDEADBEEFu);
      Canvas c(w, h, storage.data() + 1), other(w, h);
      for (int e = 0; e < 5; ++e) {
        std::vector<int64_t> frames;
        for (int64_t f = 0; f < 40; ++f) frames.push_back(f);
        for (int64_t f : {127LL, 300LL, 511LL, 512LL, 2147483648LL, 900000000000LL}) frames.push_back(f);
        for (int64_t f : frames) {
          first[e]->render(c, f);
          second[e]->render(other, f);
          TEST_ASSERT_EQUAL_UINT32_ARRAY(other.data(), c.data(), c.size());
          TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.front());
          TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, storage.back());
        }
      }
    }
}

#define FOR_EACH_SIZE for (int w : {32, 53}) for (int h = 8; h <= 16; ++h)

static int bricksLeft(const fx::Breakout& g) {
  int n = 0;
  for (int r = 0; r < g.rows; ++r) n += __builtin_popcountll(g.bricks[r]);
  return n;
}

// 3 lit LEDs and 1 dark per brick, every column of bricks, one hue per row.
static void test_brick_geometry() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    BrickBreakerEffect e;
    e.render(c, 0);
    fx::Breakout g;
    g.reset(w, h, 1u);
    TEST_ASSERT_EQUAL_INT(w / 4, g.cols);
    for (int y = g.top; y <= g.top + g.rows; ++y) {
      std::vector<int> runs;
      int run = 0, first = -1, last = -1;
      for (int x = 0; x <= w; ++x) {
        if (x < w && c.getPixel(x, y)) {
          if (first < 0) first = x;
          last = x;
          ++run;
        } else if (run) {
          runs.push_back(run);
          run = 0;
        }
      }
      if (y == g.top + g.rows) {
        TEST_ASSERT_EQUAL_INT(0, static_cast<int>(runs.size()));
        continue;
      }
      TEST_ASSERT_EQUAL_INT(g.cols, static_cast<int>(runs.size()));
      for (int r : runs) TEST_ASSERT_EQUAL_INT(3, r);
      TEST_ASSERT_EQUAL_INT(g.left, first);
      TEST_ASSERT_EQUAL_INT(4 * g.cols - 1, last - first + 1);
      if (y > g.top) TEST_ASSERT_NOT_EQUAL(c.getPixel(first, y - 1), c.getPixel(first, y));
    }
  }
}

// One brick per hit, the ball never on a live brick, both return angles, most rows reached.
static void test_brick_play() {
  FOR_EACH_SIZE {
    fx::Breakout g;
    g.reset(w, h, 0x9E3779B9u);
    const int full = bricksLeft(g);
    int least = full;
    std::set<int> speeds;
    std::vector<bool> rows(h, false);
    for (int s = 0; s < 6000; ++s) {
      const int before = bricksLeft(g);
      g.step();
      const int after = bricksLeft(g);
      TEST_ASSERT_TRUE(after == before || after == before - 1 || after == full);
      int col, row;
      if (!(after == full && before != full)) TEST_ASSERT_FALSE(g.brick(g.bx, g.by, col, row));
      speeds.insert(std::abs(g.dx));
      rows[g.by] = true;
      least = std::min(least, after);
    }
    TEST_ASSERT_TRUE(least < full);
    TEST_ASSERT_TRUE(speeds.count(1) && speeds.count(2));
    TEST_ASSERT_TRUE(std::count(rows.begin(), rows.end(), true) > h / 2);
  }
}

static bool touching(int ax, int ay, int bx, int by, int w, int h) {
  const int dx = std::abs(ax - bx), dy = std::abs(ay - by);
  return std::min(dx, w - dx) + std::min(dy, h - dy) == 1;
}

// Food, steering, growth by one, a connected body that never overlaps, and a restart.
static void test_snake_game() {
  FOR_EACH_SIZE {
    fx::SnakeRun g;
    g.reset(w, h, 0x1234567u);
    std::vector<bool> rows(h, false);
    int eats = 0, restarts = 0, moves = 0, closer = 0;
    for (int s = 0; s < 8000; ++s) {
      if (!g.ending) {
        TEST_ASSERT_FALSE(g.occupied(g.appleX, g.appleY, g.len));
        for (int i = 1; i < g.len; ++i) {
          TEST_ASSERT_TRUE(touching(g.segX(i), g.segY(i), g.segX(i - 1), g.segY(i - 1), w, h));
          for (int j = 0; j < i; ++j)
            TEST_ASSERT_FALSE(g.segX(i) == g.segX(j) && g.segY(i) == g.segY(j));
        }
      }
      rows[g.segY(0)] = true;
      const int len = g.len, ax = g.appleX, ay = g.appleY, ending = g.ending;
      const int dist = g.wrapDistance(g.segX(0), ax, w) + g.wrapDistance(g.segY(0), ay, h);
      g.step();
      if (ending) {
        if (!g.ending) {
          ++restarts;
          TEST_ASSERT_EQUAL_INT(3, g.len);
        }
        continue;
      }
      if (g.len == len + 1) {
        ++eats;
        if (!g.ending) TEST_ASSERT_TRUE(g.appleX != ax || g.appleY != ay);
      } else if (!g.ending) {
        ++moves;
        closer += g.wrapDistance(g.segX(0), ax, w) + g.wrapDistance(g.segY(0), ay, h) < dist;
      }
    }
    TEST_ASSERT_TRUE(eats > 3);
    TEST_ASSERT_TRUE(restarts >= 1);
    TEST_ASSERT_TRUE(closer * 10 > moves * 7);
    TEST_ASSERT_EQUAL_INT(h, static_cast<int>(std::count(rows.begin(), rows.end(), true)));
  }
}

static void test_snake_body_gradient() {
  Canvas c(32, 8);
  SnakeEffect e;
  e.render(c, 0);
  std::set<uint32_t> body;
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      if (c.getPixel(x, y) && c.getPixel(x, y) != 0xFF1000u) body.insert(c.getPixel(x, y));
  TEST_ASSERT_TRUE(body.size() >= 2);
}

// Paddles on both outer columns, and both of them return the ball.
static void test_pingpong_paddles_and_ball() {
  FOR_EACH_SIZE {
    Canvas c(w, h);
    PingPongEffect e;
    const int paddle = std::min(h, std::max(2, h * 3 / 8));
    for (int64_t f = 0; f < 400; ++f) {
      e.render(c, f);
      for (int side : {0, w - 1}) {
        int n = 0, top = -1;
        for (int y = 0; y < h; ++y)
          if (c.getPixel(side, y) == 0xFFFFFFu) {
            if (top < 0) top = y;
            ++n;
          }
        TEST_ASSERT_EQUAL_INT(paddle, n);
        for (int y = top; y < top + paddle; ++y) TEST_ASSERT_EQUAL_HEX32(0xFFFFFFu, c.getPixel(side, y));
      }
    }
    fx::Pong g;
    g.reset(w, h, 7u);
    int returns[2] = {0, 0};
    for (int s = 0; s < 6000; ++s) {
      const int dx = g.dx;
      const bool out = g.out;
      g.step();
      if (!out && !g.out && g.dx != dx) ++returns[dx < 0 ? 0 : 1];
    }
    TEST_ASSERT_TRUE(returns[0] >= 3 && returns[1] >= 3);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_brick_geometry);
  RUN_TEST(test_brick_play);
  RUN_TEST(test_snake_game);
  RUN_TEST(test_snake_body_gradient);
  RUN_TEST(test_pingpong_paddles_and_ball);
  RUN_TEST(test_swirls_span_and_travel);
  RUN_TEST(test_determinism_and_no_writes_outside_canvas);
  return UNITY_END();
}
