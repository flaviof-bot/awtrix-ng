#pragma once

#include <string_view>
#include "persistence/ConfigNeed.h"

namespace awtrix {
namespace cfgrules { struct ConfigError; }
// The Galactic Unicorn's panel and GPIO are fixed.
// M3b: offers() and ignoredFields() hide the panel and pin keys once persistence links.
struct PlatformConfig {
#define PLATFORM_CONFIG(type, member, initial, key, secret, need) type member = initial;
#include "platform_settings/ConfigFields.inc"
#undef PLATFORM_CONFIG
  static constexpr bool panelConfigurable = false;
  static constexpr bool gpioConfigurable = false;
  static constexpr bool validate(cfgrules::ConfigError&) { return true; }
  static constexpr bool offers(ConfigNeed) { return true; }
  static constexpr bool offers(std::string_view) { return true; }
  using Ignore = bool (*)(std::string_view, const void*);
  static constexpr Ignore ignoredFields() { return nullptr; }
};
}
