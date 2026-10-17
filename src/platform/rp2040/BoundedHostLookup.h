#pragma once

#include <cstdint>
#include <string>

#include "core/net/HostName.h"
#include "core/net/LinkStatus.h"

namespace awtrix::platform::pico {

// arduino-pico resolves .local through lwIP mDNS. Each call is bounded; unlike ESP32 this
// needs no worker task. The template lets host tests enforce timeouts and cache invalidation.
template <class Address>
class BoundedHostLookup {
 public:
  static constexpr int kTimeoutMs = 1000;

  template <class Wifi>
  bool resolve(Wifi& wifi, const std::string& host) {
    if (!wifi.isConnected()) {
      forget();
      error_ = net::LinkError::NoWifi;
      return false;
    }
    if (have_ && host == host_) return true;
    forget();
    uint32_t literal = 0;
    Address found;
    bool ok = net::parseIpv4(host, literal);
    if (ok) {
      found = Address(static_cast<uint8_t>(literal >> 24), static_cast<uint8_t>(literal >> 16),
                      static_cast<uint8_t>(literal >> 8), static_cast<uint8_t>(literal));
    } else if (!host.empty()) {
      ok = wifi.hostByName(host.c_str(), found, kTimeoutMs) == 1;
      if (!ok && net::isMdnsName(host))
        ok = wifi.hostByName(net::mdnsLabel(host).c_str(), found, kTimeoutMs) == 1;
    }
    if (!ok || static_cast<uint32_t>(found) == 0) {
      error_ = net::LinkError::HostNotFound;
      return false;
    }
    host_ = host;
    address_ = found;
    have_ = true;
    error_ = net::LinkError::None;
    return true;
  }

  Address address() const { return address_; }
  net::LinkError error() const { return error_; }
  void forget() {
    have_ = false;
    host_.clear();
    address_ = Address();
    error_ = net::LinkError::None;
  }

 private:
  std::string host_;
  Address address_;
  net::LinkError error_ = net::LinkError::None;
  bool have_ = false;
};
}
