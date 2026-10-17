#pragma once
#include <Arduino.h>
#include "platform/rp2040/PicoResetReason.h"

namespace awtrix::platform {
// The RP2040's reset cause under the shared resetReason names; the ESP32 mapping stays in
// transport/DeviceStateJson.cpp.
inline const char* resetReasonName() {
  // setup calls this before arming: the watchdog overwrites the framework scratch register.
  static const char* const reason = picoResetReasonName(rp2040.getResetReason());
  return reason;
}
}
