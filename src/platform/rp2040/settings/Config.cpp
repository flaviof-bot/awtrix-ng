#include "platform_settings/Config.h"
#include "persistence/DeviceConfigRows.h"

namespace awtrix {
bool PlatformConfig::offers(std::string_view key) {
  for (const auto& row : configfields::kRows)
    if (key == row.key) return offers(row.need);
  return true;
}
}
