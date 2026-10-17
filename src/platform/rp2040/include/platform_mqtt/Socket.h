#pragma once

#include <WiFiClient.h>
#include "system/Watchdog.h"
#include "transport/net/HostResolver.h"

namespace awtrix {
// The ESP32's plain socket, with arduino-pico's limits: its client timeout also bounds TCP connect
// and write, not just reads, and the blocking connect feeds the hardware watchdog.
class MqttSocket {
 public:
  struct Options {};
  static constexpr unsigned long kTimeoutMs = 300;
  void begin(Options, const std::string&) { wifi_.setTimeout(kTimeoutMs); }
  void shutdown() { wifi_.stop(); }
  Client& socket() { return wifi_; }
  static constexpr bool connecting() { return false; }
  template <typename Connect>
  net::ResolveState connectStep(Connect connect) {
    watchdog::feed();
    const bool ok = connect();
    watchdog::feed();
    return ok ? net::ResolveState::Ready : net::ResolveState::Failed;
  }
 private:
  WiFiClient wifi_;
};
}
