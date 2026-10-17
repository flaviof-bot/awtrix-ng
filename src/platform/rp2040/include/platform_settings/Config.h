#pragma once

#include <string_view>
#include "persistence/ConfigNeed.h"

namespace awtrix {
namespace cfgrules { struct ConfigError; }
// The Galactic Unicorn's panel and GPIO are fixed: their keys are not offered, so an edit or a
// restored backup leaves the board's own values in place.
struct PlatformConfig {
#define PLATFORM_CONFIG(type, member, initial, key, secret, need) type member = initial;
#include "platform_settings/ConfigFields.inc"
#undef PLATFORM_CONFIG
  static constexpr bool panelConfigurable = false;
  static constexpr bool gpioConfigurable = false;
  static constexpr bool validate(cfgrules::ConfigError&) { return true; }
  static constexpr bool offers(ConfigNeed need) { return need == ConfigNeed::None; }
  static bool offers(std::string_view key);
  using Ignore = bool (*)(std::string_view, const void*);
  static constexpr Ignore ignoredFields() { return ignore; }
 private:
  static bool ignore(std::string_view key, const void*) { return !offers(key); }
};
}
