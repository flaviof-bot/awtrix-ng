#include "platform/rp2040/RadioStartup.h"
#include <hardware/pio.h>
#include "system/Log.h"

// The Pico W variants call this before setup(). Defer it until board->begin()
// has claimed the display SM/program and DMA channels. Do not override the
// entire variant (which would discard future board startup work).
extern "C" void __wrap_init_cyw43_wifi() {}
extern "C" void __real_init_cyw43_wifi();

namespace awtrix::platform {
void beginRadio() {
  static bool started = false;
  if (started) return;
  started = true;
  bool claimed[NUM_PIOS][NUM_PIO_STATE_MACHINES]{};
  for (uint p = 0; p < NUM_PIOS; ++p)
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm)
      claimed[p][sm] = pio_sm_is_claimed(pio_get_instance(p), sm);
  __real_init_cyw43_wifi();
  // Query the SDK allocation bitmap, not a guessed SM or private bus_data ABI.
  // No other allocator runs between these snapshots on this core.
  bool found = false;
  for (uint p = 0; p < NUM_PIOS; ++p) {
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; ++sm) {
      if (!claimed[p][sm] && pio_sm_is_claimed(pio_get_instance(p), sm)) {
        logf("wifi: CYW43 claimed PIO%u SM%u after display startup", p, sm);
        found = true;
      }
    }
  }
  if (!found) logf("wifi: CYW43 PIO allocation not observed; check radio initialization");
}
}
