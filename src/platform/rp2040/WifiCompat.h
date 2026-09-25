#pragma once

#include <cstdint>
#include <cstring>

// Template seam keeps arduino-pico's differing API contract host-testable.
// No board includes: callers supply WiFi, IPAddress and the mode constants.

namespace awtrix::platform::pico {
// Pico scanComplete() returns 0 both before any scan and for an empty result.
// Track the request lifecycle so an empty scan can finish and the next starts anew.
class WifiScan {
 public:
  template <class Wifi> int poll(Wifi& wifi) {
    if (!started_) {
      wifi.scanNetworks(true);
      started_ = true;
      return -1;
    }
    return wifi.scanComplete();
  }
  void consumed() { started_ = false; }
 private:
  bool started_ = false;
};

template <class Wifi, class Address>
void configureStatic(Wifi& wifi, Address ip, Address gateway, Address subnet,
                     Address dns1, Address dns2) {
  // Pico uses (ip, dns, gateway, subnet), unlike ESP32's five-argument overload.
  wifi.config(ip, dns1, gateway, subnet);
  wifi.setDNS(dns1, dns2);
}

// Index of the strongest scan result whose SSID is exactly `ssid`, or -1. ESP32 gets this from
// WIFI_CONNECT_AP_BY_SIGNAL; arduino-pico joins whichever BSSID the radio finds first, which in a
// multi-AP home can be a distant one.
template <class SsidAt, class RssiAt>
int strongestMatch(int count, const char* ssid, SsidAt ssidAt, RssiAt rssiAt) {
  int best = -1;
  int bestRssi = 0;
  for (int i = 0; i < count; ++i) {
    const char* s = ssidAt(i);
    if (s == nullptr || std::strcmp(s, ssid) != 0) continue;
    const int rssi = rssiAt(i);
    if (best < 0 || rssi > bestRssi) {
      best = i;
      bestRssi = rssi;
    }
  }
  return best;
}

// arduino-pico's begin() does not block once mode() has been called, and calling it again restarts
// the association. Only start a new join once the previous one has had its full timeout.
// 32-bit like the Pico's millis(), so the subtraction survives its 49-day wrap.
inline bool joinDue(uint32_t nowMs, uint32_t lastJoinMs, bool joinedBefore, uint32_t timeoutMs) {
  return !joinedBefore || nowMs - lastJoinMs >= timeoutMs;
}

struct JoinResult {
  bool pinned = false;  // joined a specific BSSID found by the scan
  int rssi = 0;
  uint8_t bssid[6] = {};
};

// `pin` = false skips the scan and lets the radio choose, the fallback when a pinned attempt failed
// (hidden SSID, or the chosen AP went away between scan and join).
template <class Wifi, class Mode>
JoinResult join(Wifi& wifi, Mode mode, const char* ssid, const char* password, bool pin = true) {
  // The pinned core's begin() changes _mode to STA even in AP_STA. Restore
  // AP_STA after each attempt so the NEXT retry cannot tear down the portal.
  wifi.mode(mode);
  JoinResult r;
  const int found = pin ? wifi.scanNetworks(false) : 0;
  const int best = strongestMatch(
      found, ssid, [&](int i) { return wifi.SSID(static_cast<uint8_t>(i)); },
      [&](int i) { return static_cast<int>(wifi.RSSI(static_cast<uint8_t>(i))); });
  if (best >= 0) {
    r.pinned = true;
    r.rssi = static_cast<int>(wifi.RSSI(static_cast<uint8_t>(best)));
    wifi.BSSID(static_cast<uint8_t>(best), r.bssid);
  }
  if (pin) wifi.scanDelete();
  if (r.pinned)
    wifi.begin(ssid, password, r.bssid);
  else
    wifi.begin(ssid, password);
  wifi.mode(mode);
  return r;
}
}
