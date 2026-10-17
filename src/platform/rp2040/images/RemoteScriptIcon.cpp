#include "media/ScriptIcon.h"

namespace awtrix {

// As on the ESP32: no remote image source, so a remote icon name draws nothing.
void ScriptIconSet::loadRemote(Entry& e, std::string_view, int64_t) {
  e.state = State::kMissing;
  e.retryStep = 0;
}

}
