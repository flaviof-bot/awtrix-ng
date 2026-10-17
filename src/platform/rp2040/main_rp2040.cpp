#include <Arduino.h>

#include <memory>
#include <string>

#include "AppConfig.h"
#include "core/ApplicationRuntime.h"
#include "core/FrameClock.h"
#include "core/PlatformDescriptor.h"
#include "core/ScreenDisplay.h"
#include "core/apps/DefaultApps.h"
#include "hal/BoardRegistry.h"
#include "hal/GalacticUnicornBoard.h"
#include "hal/IBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "system/MonotonicClock.h"

// Feature macros describe this build, not a claim that a runtime service exists.
static_assert(!AWTRIX_FEATURE_SCRIPTING && !AWTRIX_FEATURE_MP3 &&
              !AWTRIX_FEATURE_RADIO && !AWTRIX_FEATURE_OUTBOUND_TLS &&
              !AWTRIX_FEATURE_BROWSER_OTA, "Pico build must not enable unsupported services");

using namespace awtrix;

namespace {
// M4a2: replaced by the Pico's time service. Until then built-ins render their unset-clock state.
class UnsetClock final : public IPageClock {
 public:
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    ctx.epochMs = -1;
  }
};

// M4a2: sleep; M3b: factory and settings reset once preferences live in LittleFS.
class PicoSystem final : public ISystemService {
 public:
  void reboot() override { rebootPending = true; }
  void sleep(uint64_t) override { Serial.println("sleep unavailable"); }
  void factoryReset() override { Serial.println("persistent config unavailable"); }
  void resetSettings() override { Serial.println("persistent settings unavailable"); }
  bool rebootPending = false;
};

// The panel is fixed: 53x11, nothing else is admitted.
constexpr DisplayLimits kUnicornDisplayLimits{53, 53, 11, 11, 53 * 11};

IBoard* g_board = nullptr;
sound::AudioRouter g_audio; // Null sinks honestly report MP3/radio unavailable.
ScreenDisplay g_display;
PicoSystem g_system;
UnsetClock g_pageClock;
bool g_settingsDirty = false; // M3b: saved to LittleFS.
std::unique_ptr<DefaultApps> g_builtinApps;
std::unique_ptr<ApplicationRuntime> g_runtime;
std::string g_capabilities; // M4b: served by the HTTP API, M5b: published over MQTT.

PlatformDescriptor platformDescriptor() {
  PlatformDescriptor platform{"rp2040", {g_board->matrixWidth(), g_board->matrixHeight(), false},
                              false};
  platform.lightSensor = true;
  platform.microphonePcm = false;
  platform.display.limits = kUnicornDisplayLimits;
  platform.display.ready = g_board->displayReady() && g_runtime->ready();
  return platform;
}

void paceFrame() {
  static int64_t nextMs = 0;
  const int64_t now = monotonicMs();
  if (nextMs <= now) {
    nextMs = now + kFramePeriodMs;
    return;
  }
  delay(static_cast<unsigned long>(nextMs - now));
  nextMs += kFramePeriodMs;
}
}

void setup() {
  Serial.begin(115200);
  // Never wait for a USB host: the panel must boot with power alone.
  const DeviceConfig cfg = galacticUnicornDefaults(); // M3b: loaded from LittleFS.
  g_board = &activeBoard(cfg);
  g_audio.setTone(g_board->toneSink());
  g_audio.setTrack(g_board->trackSink());
  // M3b: icons from LittleFS.
  ApplicationServices services{g_audio, g_display, g_system, g_pageClock, awtrixFontCatalog(),
      nullptr, nullptr, [](uint8_t brightness) { g_board->setBrightness(brightness); }};
  g_builtinApps.reset(new DefaultApps);
  g_runtime.reset(new ApplicationRuntime(g_board->matrixWidth(), g_board->matrixHeight(), services,
                                          g_builtinApps->registry()));
  if (!g_runtime->ready()) Serial.println("matrix: no frame memory");
  g_board->begin();
  g_display.setScreen(&g_runtime->canvas());
  g_runtime->markSensors(*g_board);
  g_runtime->bindOutputs(*g_board, g_settingsDirty);
  g_runtime->engine().state().emit(StateEvent::SettingsChanged);
  g_capabilities = g_runtime->capabilitiesJson(platformDescriptor());
  Serial.printf("boot: AWTRIX NG %s on %s (%dx%d); no network/panel driver yet\n",
                AWTRIX_NG_VERSION, g_board->name(), g_board->matrixWidth(),
                g_board->matrixHeight());
}

void loop() {
  const int64_t now = monotonicMs();
  g_runtime->beginFrame(now);
  g_runtime->tick(now); // M6a: the script host.
  g_runtime->render(now);
  g_board->show(g_runtime->canvas());
  if (g_system.rebootPending && !g_runtime->powerBusy()) rp2040.reboot();
  paceFrame();
}
