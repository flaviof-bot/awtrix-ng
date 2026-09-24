#pragma once

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

template <class Wifi, class Mode>
void join(Wifi& wifi, Mode mode, const char* ssid, const char* password) {
  // The pinned core's begin() changes _mode to STA even in AP_STA. Restore
  // AP_STA after each attempt so the NEXT retry cannot tear down the portal.
  wifi.mode(mode);
  wifi.begin(ssid, password);
  wifi.mode(mode);
}
}
