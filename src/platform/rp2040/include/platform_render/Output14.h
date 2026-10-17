#pragma once
#include <cmath>
#include <cstdint>

namespace awtrix::render {
// Native PWM output: quantise once to the Unicorn's fourteen binary planes.
// Kept separate from ColorGrade's 8-bit preview output and 16-bit gamma floor:
// the device must preserve PR 1's exact dark-level and truncation behaviour.
inline uint16_t unicornOutput14(uint8_t code, float gamma, uint8_t brightness, uint8_t scale) {
  const float f = code / 255.0f;
  const auto light = static_cast<uint32_t>(std::lround(
      (gamma > 0 ? std::pow(f, gamma) : f) * 16383.0f));
  return light * brightness / 255u * scale / 255u;
}
}
