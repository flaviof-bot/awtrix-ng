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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_swirls_span_and_travel);
  RUN_TEST(test_determinism_and_no_writes_outside_canvas);
  return UNITY_END();
}
