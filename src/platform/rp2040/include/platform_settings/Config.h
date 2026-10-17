#pragma once

#include <string_view>
#include "persistence/ConfigNeed.h"

namespace awtrix {
namespace cfgrules { struct ConfigError; }
// Read fixed fields so interactive edits can be validated rather than silently discarded.
// SystemConfigApply keeps the board's own fields on backup restore.
struct PlatformConfig {
#define PLATFORM_CONFIG(type, member, initial, key, secret, need) type member = initial;
#include "platform_settings/ConfigFields.inc"
#undef PLATFORM_CONFIG
  static constexpr bool panelConfigurable = true;
  static constexpr bool gpioConfigurable = false;
  static constexpr bool validate(cfgrules::ConfigError&) { return true; }
  static constexpr bool offers(ConfigNeed) { return true; }
  static bool offers(std::string_view key);
  using Ignore = bool (*)(std::string_view, const void*);
  static constexpr Ignore ignoredFields() { return ignore; }
 private:
  static bool ignore(std::string_view key, const void*) { return !offers(key); }
};
}
