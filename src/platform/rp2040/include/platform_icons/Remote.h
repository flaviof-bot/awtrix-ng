#pragma once

namespace awtrix {

// M6a (#6): script icons; no remote icon source until then.
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
