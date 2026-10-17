#pragma once

namespace awtrix {

// Script icons come from the device only, as on the ESP32; the Linux app also takes them from a
// remote image source (its own platform_icons/Remote.h).
struct RemoteIconEntry {
  static void resetRemote() {}
  static constexpr int offsetX() { return 0; }
  static constexpr int offsetY() { return 0; }
};

class RemoteIconSource {
 public:
  static constexpr bool remoteChanged(const RemoteIconEntry&) { return false; }
};

}
