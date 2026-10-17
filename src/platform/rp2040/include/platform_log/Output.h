#pragma once

#include <Arduino.h>

namespace awtrix {
// M6a (#9): boot and driver logs share this sink.
inline void writeLogLine(const char* line) { Serial.println(line); }
}
