#include <unity.h>
#include "../EngineFakes.h"
#include "core/ApplicationRuntime.h"
#include "core/apps/DefaultApps.h"
#include "media/AwtrixFontAdapter.h"
using namespace awtrix;
void setUp() {} void tearDown() {}
struct Clock : IPageClock { void fill(RenderCtx&, int64_t) override {} };
void builtin_catalog_parity() {
  sound::AudioRouter audio;
  test::NullDisplay display;
  test::NullSystem system;
  Clock clock;
  DefaultApps esp, pico;
  ApplicationServices services{audio, display, system, clock, awtrixFontCatalog()};
  ApplicationRuntime a(32,8,services,esp.registry()), b(53,11,services,pico.registry());
  TEST_ASSERT_EQUAL(5,b.apps().size());
  TEST_ASSERT_TRUE(a.apps().ids() == b.apps().ids());
  TEST_ASSERT_EQUAL(19,b.effects().size());
  TEST_ASSERT_EQUAL(6,b.overlays().size());
  TEST_ASSERT_TRUE(a.effects().names() == b.effects().names());
  TEST_ASSERT_TRUE(a.effects().paletteNames() == b.effects().paletteNames());
  TEST_ASSERT_TRUE(a.overlays().names() == b.overlays().names());
  const std::vector<std::string> expected = {"BrickBreaker","Checkerboard","ColorWaves","Fade",
    "Fireworks","LookingEyes","Matrix","MovingLine","Pacifica","PingPong","Plasma","PlasmaCloud",
    "Radar","Ripple","Snake","SwirlIn","SwirlOut","TheaterChase","TwinklingStars"};
  TEST_ASSERT_TRUE(expected == b.effects().names());
  // PR 1 counted 15 on 1.2.1; 1.2.2's shared registry has 16 palette effects on every board.
  TEST_ASSERT_EQUAL(16,b.effects().paletteNames().size());
  const std::vector<std::string> overlays = {"drizzle","frost","rain","snow","storm","thunder"};
  TEST_ASSERT_TRUE(overlays == b.overlays().names());
}
int main() { UNITY_BEGIN(); RUN_TEST(builtin_catalog_parity); return UNITY_END(); }
