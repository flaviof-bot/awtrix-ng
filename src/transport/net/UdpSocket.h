#pragma once

#include <cstddef>
#include <cstdint>

#include "core/net/Datagram.h"
#if defined(AWTRIX_PLATFORM_RP2040)
#include <WiFiUdp.h>
#endif

namespace awtrix {

class UdpSocket {
 public:
  ~UdpSocket() { close(); }

  bool open(uint16_t port);
  void close();
  bool isOpen() const { return fd_ >= 0; }

  int receive(void* buf, std::size_t cap);
  int receiveFrom(void* buf, std::size_t cap, net::Endpoint& from);

  bool replyTo(uint16_t port, const void* data, std::size_t len);
  bool sendTo(const net::Endpoint& to, const void* data, std::size_t len);

 private:
#if defined(AWTRIX_PLATFORM_RP2040)
  WiFiUDP udp_;  // arduino-pico has no BSD sockets; isOpen() still reads fd_
#endif
  int fd_ = -1;
  uint32_t peerAddr_ = 0;
  bool havePeer_ = false;
};

}
