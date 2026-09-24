#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#if defined(AWTRIX_PLATFORM_RP2040)
#include <pico/time.h>
#include "system/PicoSleep.h"
#else
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#endif
#include <functional>
#include "core/PinRules.h"
#include "core/Services.h"
#include "core/render/Canvas.h"
#include "hal/IBoard.h"
#include "transport/DeviceStateJson.h"

namespace awtrix {
using Publisher = std::function<void(const std::string& suffix, const std::string& payload)>;
class DeviceDisplay : public IDisplayService {
 public:
  void setPublisher(Publisher pub) { pub_ = std::move(pub); }
  void setScreen(Canvas* screen) { screen_ = screen; }
  void sendScreen() override {
    if (pub_ && screen_) pub_("state/screen", buildScreenJson(*screen_));
  }
 private:
  Publisher pub_;
  Canvas* screen_ = nullptr;
};

class DeviceSystem : public ISystemService {
 public:
  enum class Pending { None, Reboot, Sleep, FactoryReset, ResetSettings };
  // Record intent so transports can send their response before runPending().
  void reboot() override { pending_ = Pending::Reboot; }
  void sleep(uint64_t durationMs) override {
    if (durationMs == 0) return;
    sleepMs_ = durationMs;
    pending_ = Pending::Sleep;
  }
  void factoryReset() override { pending_ = Pending::FactoryReset; }
  void resetSettings() override { pending_ = Pending::ResetSettings; }
  void setWakeButtonPin(int pin) {
#if defined(AWTRIX_PLATFORM_RP2040)
    wakePin_ = pin == 27 ? pin : -1;
#else
    // Only RTC pins survive ESP32 deep sleep; otherwise timer wake only.
    wakePin_ = pins::isRtcWakePin(pin) ? pin : -1;
#endif
  }
  void setDisplayOff(std::function<void()> fn) { displayOff_ = std::move(fn); }
  bool hasPending() const { return pending_ != Pending::None; }
  bool resetsSettings() const {
    return pending_ == Pending::FactoryReset || pending_ == Pending::ResetSettings;
  }
  void runPending() {
    const Pending p = pending_;
    pending_ = Pending::None;
    switch (p) {
      case Pending::Reboot:
        delay(200);
        restart();
        break;
      case Pending::Sleep:
        if (displayOff_) displayOff_();
#if defined(AWTRIX_PLATFORM_RP2040)
        delay(200);
        WiFi.aggressiveLowPowerMode();
        {
          const auto pressed = [this] { return wakePin_ >= 0 && digitalRead(wakePin_) == LOW; };
          PicoSleep sleeper(time_us_64() / 1000, sleepMs_, pressed());
          while (!sleeper.wake(time_us_64() / 1000, pressed())) delay(10);
        }
        // Like ESP32 deep-sleep wake, begin a fresh application session.
        restart();
#else
        esp_sleep_enable_timer_wakeup(sleepMs_ * 1000ULL);
        if (wakePin_ >= 0) {
          const gpio_num_t g = static_cast<gpio_num_t>(wakePin_);
          rtc_gpio_pullup_en(g);
          rtc_gpio_pulldown_dis(g);
          esp_sleep_enable_ext0_wakeup(g, 0);
        }
        delay(200);
        esp_deep_sleep_start();
#endif
        break;
      case Pending::FactoryReset:
        // Include legacy namespaces so old settings cannot reappear.
        clearNvs("awtrix-ng");
        clearNvs("awtrix-cfg");
        clearNvs("awtrix");
        LittleFS.format();
#if defined(AWTRIX_PLATFORM_RP2040)
        WiFi.disconnect(true);
#else
        WiFi.disconnect(true, true);
#endif
        delay(300);
        restart();
        break;
      case Pending::ResetSettings:
        clearNvs("awtrix-ng");
        clearNvs("awtrix");
        delay(200);
        restart();
        break;
      case Pending::None: break;
    }
  }
 private:
  static void restart() {
#if defined(AWTRIX_PLATFORM_RP2040)
    rp2040.reboot();
#else
    ESP.restart();
#endif
  }
  static void clearNvs(const char* ns) {
    Preferences p;
    p.begin(ns, false);
    p.clear();
    p.end();
  }
  Pending pending_ = Pending::None;
  uint64_t sleepMs_ = 0;
  int wakePin_ = -1;
  std::function<void()> displayOff_;
};
}
