#pragma once

#include <cstdint>
#include <pico/time.h>

namespace awtrix {
inline int64_t monotonicUs() { return static_cast<int64_t>(time_us_64()); }
inline int64_t monotonicMs() { return monotonicUs() / 1000; }
}
