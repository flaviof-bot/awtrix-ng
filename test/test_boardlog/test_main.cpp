#include <unity.h>

#include <string>

#include "core/api/JsonStream.h"
#include "hal/BoardLog.h"
#include "system/BoardLogSink.h"
#include "system/Log.h"

using namespace awtrix;

namespace {
std::string tail() {
  std::string out;
  api::JsonStream stream(
      [](void* ctx, const char* data, std::size_t size) {
        static_cast<std::string*>(ctx)->append(data, size);
      },
      &out);
  logbuf::streamJsonAfter(0, stream);
  return out;
}
bool logged(const char* text) { return tail().find(text) != std::string::npos; }
}

void setUp() {}
void tearDown() { logbuf::setVerbose(false); }

// Fork issue #9: a Pico board line reaches the buffer /api/v1/logs reads.
void board_line_lands_in_the_shared_log() {
  boardLogf(boardLogLine, false, "display: refresh unavailable after WiFi init");
  TEST_ASSERT_TRUE(logged("display: refresh unavailable after WiFi init"));
  boardLogf(boardLogLine, false, "tone: %s; new PIO-SM mask 0x%lx", "unavailable", 0x10ul);
  TEST_ASSERT_TRUE(logged("tone: unavailable; new PIO-SM mask 0x10"));
}

// The PIO and DMA claims are verbose: in the log only with debugMode on.
void verbose_board_line_follows_debug_mode() {
  logbuf::setVerbose(false);
  boardLogf(boardLogLine, true, "display: PIO%u SM%d DMA%d+%d; CYW43 starts next", 1u, 0, 4, 5);
  TEST_ASSERT_FALSE(logged("display: PIO1 SM0 DMA4+5"));
  logbuf::setVerbose(true);
  boardLogf(boardLogLine, true, "display: PIO%u SM%d DMA%d+%d; CYW43 starts next", 1u, 2, 6, 7);
  TEST_ASSERT_TRUE(logged("display: PIO1 SM2 DMA6+7; CYW43 starts next"));
}

// A board without a sink (host tests, early boot) stays silent rather than crashing.
void no_sink_is_silent() { boardLogf(nullptr, false, "display: %s", "nothing"); }

int main() {
  UNITY_BEGIN();
  RUN_TEST(board_line_lands_in_the_shared_log);
  RUN_TEST(verbose_board_line_follows_debug_mode);
  RUN_TEST(no_sink_is_silent);
  return UNITY_END();
}
