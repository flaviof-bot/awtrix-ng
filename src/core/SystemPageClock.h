#pragma once

#include <chrono>
#include <cstdint>

#include <ctime>

#include "core/Services.h"
#include "core/render/RenderPipeline.h"

namespace awtrix {

class SystemPageClock : public IPageClock {
 public:
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    const auto now = std::chrono::system_clock::now();
    const int64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch())
                                .count();
    fillAt(ctx, nowMs, epochMs);
  }
  // fill() at a given wall-clock time, so a host test can check the conversion.
  static void fillAt(RenderCtx& ctx, int64_t nowMs, int64_t epochMs) {
    ctx.nowMs = nowMs;
    const std::time_t t = static_cast<std::time_t>(epochMs / 1000);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    ctx.hour = tmv.tm_hour;
    ctx.minute = tmv.tm_min;
    ctx.second = tmv.tm_sec;
    ctx.weekday = tmv.tm_wday;
    ctx.mday = tmv.tm_mday;
    ctx.month = tmv.tm_mon + 1;
    ctx.year = tmv.tm_year + 1900;
    // -1 marks a clock that has not been set.
    ctx.epochMs = ctx.year >= 2020 ? epochMs : -1;
  }
};

}
