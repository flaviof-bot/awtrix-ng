#pragma once
#include "hal/GalacticUnicornDisplay.h"
#include "core/CoreEngine.h"
#include "core/input/ButtonInput.h"
#include "core/sensing/AutoBrightness.h"
#include "core/sensing/MedianFilter.h"

namespace awtrix {
// Board-only adapter. Navigation/menu and button events remain shared with ESP32.
class GalacticUnicornControls {
 public:
  void begin(CoreEngine& engine, const DeviceConfig& config, input::IButtonMenu* menu) {
    input_.begin(engine, config, menu);
  }
  void tick(CoreEngine& engine, const std::array<bool, 9>& pressed, int64_t nowMs) {
    using namespace galactic;
    for (size_t i = 0; i < Inputs.size(); ++i) {
      if (!buttons_[i].update(pressed[i], nowMs)) continue;
      const auto& s = engine.state().settings();
      Command c(CommandType::SetSettings);
      switch (Inputs[i].action) {
        case InputAction::Power:
          c.type = CommandType::SetDisplay;
          c.payload = engine.state().runtime().matrixOff ? "{\"power\":true}" : "{\"power\":false}";
          break;
        case InputAction::BrightnessUp:
        case InputAction::BrightnessDown:
          c.payload = "{\"autoBrightness\":false,\"brightness\":" +
              std::to_string(stepClamped(s.brightness,
                  Inputs[i].action == InputAction::BrightnessUp ? 10 : -10, 255)) + "}";
          break;
        case InputAction::VolumeUp:
        case InputAction::VolumeDown:
          c.payload = "{\"volume\":" + std::to_string(stepClamped(s.volume,
              Inputs[i].action == InputAction::VolumeUp ? 5 : -5, 100)) + "}";
          break;
        default: continue; // D unmapped. A/B/C go through ButtonRouter below.
      }
      engine.execute(c);
    }
    input_.setState({buttons_[0].pressed(), buttons_[1].pressed(), buttons_[2].pressed()}, nowMs);
  }
  // Same median and light-curve input as ESP32 PeripheryService, sampled at 100 ms.
  void sampleLight(CoreEngine& engine, int raw, int64_t nowMs) {
    if (nowMs - lastLdrMs_ < 100) return;
    lastLdrMs_ = nowMs;
    auto& rt = engine.state().runtime();
    const auto med = ldrFilter_.push(static_cast<uint16_t>(std::max(0, raw)));
    rt.ldrRaw = med;
    rt.lightLevel = lightLevelFromRaw(med, input_.config().lightConfig());
  }
 private:
  input::ButtonInput input_;
  std::array<galactic::DebouncedButton, 9> buttons_{};
  MedianFilter<uint16_t, 5> ldrFilter_;
  int64_t lastLdrMs_ = -100000;
};
}
