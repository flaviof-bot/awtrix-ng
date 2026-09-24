#include "hal/BoardRegistry.h"

#if defined(AWTRIX_PLATFORM_RP2040)
#include "hal/GalacticUnicornBoard.h"
#else
#include "hal/Esp32Board.h"
#endif

namespace awtrix {
// The first call builds the board from cfg; later changes require a reboot.
IBoard& activeBoard(const DeviceConfig& cfg) {
#if defined(AWTRIX_PLATFORM_RP2040)
  static GalacticUnicornBoard board(cfg);
#else
  static Esp32Board board(cfg);
#endif
  return board;
}
}
