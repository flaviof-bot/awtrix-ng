#include "platform/rp2040/Rp2040Profile.h"

namespace awtrix::pins {
// The platform picks the profile; src/core keeps the portable policy.
const SocProfile& activeProfile() { return rp2040Profile(); }
}
