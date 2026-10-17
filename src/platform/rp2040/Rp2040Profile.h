#pragma once

#include "core/SocProfile.h"

namespace awtrix {
namespace pins {

// Galactic Unicorn wiring, shared by Pico W and Pico 2 W. The panel has no single data-pin
// setting: -1 denotes its fixed multi-pin PIO interface. I2C is -1 as well: the Qw/ST connector is
// on GPIO 4/5, but these builds read no I2C sensor. Battery, buzzer and DFPlayer are absent.
// https://github.com/pimoroni/pimoroni-pico/blob/main/libraries/galactic_unicorn/galactic_unicorn.hpp
inline const SocProfile& rp2040Profile() {
  static constexpr PinRange adc[] = {{26, 28}};
  static constexpr ReservedRange reserved[] = {
      {13, 20, "the fixed panel interface"}, {23, 25, "the Pico wireless interface"},
      {29, 29, "the Pico wireless interface"}};
  static const SocProfile p = {
      "rp2040", "Galactic Unicorn (Pico W / Pico 2 W)", 29,
      {nullptr, 0}, {nullptr, 0}, {reserved, detail::countOf(reserved)},
      {adc, detail::countOf(adc)}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0},
      PinSet{-1, 0, 1, 3, -1, 28, -1, -1, -1, -1, -1, false, 10, 11, 9, -1, 22},
      true};
  return p;
}

}
}
