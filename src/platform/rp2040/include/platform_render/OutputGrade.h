#pragma once

#include <cstdint>

namespace awtrix::render {
// M4a1: the PIO panel driver may grade its own output; 8-bit like the ESP32 for now.
class OutputGrade {
 protected:
  static void prepareOutput() {}
  static void rememberOutput(int, int, uint16_t) {}
  static uint8_t encodeOutput(uint32_t light) { return static_cast<uint8_t>((light + 128) / 257); }
  static void settleOutput(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t*) {}
};
}
