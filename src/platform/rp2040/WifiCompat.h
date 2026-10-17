#pragma once

#include <cstdint>
#include <string>

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

// arduino-pico's begin() does not block once mode() has been called, and calling it again restarts
// the association. Only start a new join once the previous one has had its full timeout.
// 32-bit like the Pico's millis(), so the subtraction survives its 49-day wrap.
inline bool joinDue(uint32_t nowMs, uint32_t lastJoinMs, bool joinedBefore, uint32_t timeoutMs) {
  return !joinedBefore || nowMs - lastJoinMs >= timeoutMs;
}

// arduino-pico joins through the CYW43 firmware, which picks the AP for the SSID. The Pico does not
// pin a BSSID: on an eero mesh, pinned joins (the "join" iovar) to the node next to the board were
// refused with "network not found", while plain joins to that same node succeeded.
template <class Wifi, class Mode>
void join(Wifi& wifi, Mode mode, const char* ssid, const char* password) {
  // The pinned core's begin() changes _mode to STA even in AP_STA. Restore
  // AP_STA after each attempt so the NEXT retry cannot tear down the portal.
  wifi.mode(mode);
  wifi.begin(ssid, password);
  wifi.mode(mode);
  // ESP32 runs with esp_wifi_set_ps(WIFI_PS_NONE). The CYW43 default (CYW43_PERFORMANCE_PM) is
  // still power-save mode 2, which drops links on some mesh routers. begin() can re-initialise the
  // radio, so apply it after every join.
  wifi.noLowPowerMode();
}

// The pinned core's station-only begin() stops only the station, so the provisioning AP keeps
// running through a "paused" join. A second softAP() on a running AP fails, and its rollback tears
// the AP down: after the first failed retry the setup network was gone (softAPIP() unset). Stop the
// AP first so every start is a clean one. Returns whether the AP came up with an address.
template <class Wifi, class Mode>
bool startAp(Wifi& wifi, Mode apSta, const char* name) {
  wifi.disconnectAP();
  wifi.mode(apSta);
  wifi.softAP(name);
  return static_cast<bool>(wifi.softAPIP());
}

// The core prints an unset address as "(IP unset)" where ESP32 prints "0.0.0.0". Callers read
// "0.0.0.0" as "no address"; the web UI switches to its Wi-Fi form on it in setup mode.
template <class Address>
std::string addressText(const Address& ip) {
  return ip ? std::string(ip.toString().c_str()) : std::string("0.0.0.0");
}
}
