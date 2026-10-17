#include <Arduino.h>

#include <memory>
#include <string>

#include "AppConfig.h"
#include "core/ApplicationRuntime.h"
#include "core/FrameClock.h"
#include "core/PlatformDescriptor.h"
#include "core/ScreenDisplay.h"
#include "core/SettingsSaver.h"
#include "core/apps/DefaultApps.h"
#include "hal/BoardRegistry.h"
#include "hal/GalacticUnicornBoard.h"
#include "hal/IBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "media/DevicePageIcon.h"
#include "persistence/AppOrderStore.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "platform/BuildFeatures.h"
#include "platform/rp2040/PaletteFiles.h"
#include "platform/rp2040/Rp2040Profile.h"
#include "system/Log.h"

#include <LittleFS.h>
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

// M4a2: sleep. A reset removes the namespace files and reboots without saving over them.
class PicoSystem final : public ISystemService {
 public:
  void reboot() override { rebootPending = true; }
  void sleep(uint64_t) override { Serial.println("sleep unavailable"); }
  void factoryReset() override {
    LittleFS.remove("/NVS/awtrix-cfg.bin");
    resetSettings();
  }
  void resetSettings() override {
    LittleFS.remove("/NVS/awtrix-ng.bin");
    resetPending = true;
    rebootPending = true;
  }
  bool rebootPending = false;
  bool resetPending = false;
};

// Eight rows is the compatibility mode; 9 and 10 are rejected by ConfigRules.
constexpr DisplayLimits kUnicornDisplayLimits{53, 53, 8, 11, 53 * 11};

IBoard* g_board = nullptr;
sound::AudioRouter g_audio; // Null sinks honestly report MP3/radio unavailable.
ScreenDisplay g_display;
PicoSystem g_system;
UnsetClock g_pageClock;
DeviceConfig g_cfg;
bool g_storageReady = false;
bool g_settingsDirty = false;
SettingsSaver g_settingsSaver{-100000};
std::unique_ptr<DefaultApps> g_builtinApps;
std::unique_ptr<ApplicationRuntime> g_runtime;
std::string g_capabilities; // M4b: served by the HTTP API, M5b: published over MQTT.

PlatformDescriptor platformDescriptor() {
  PlatformDescriptor platform{"rp2040", {g_board->matrixWidth(), g_board->matrixHeight(), true},
                              false};
  platform.lightSensor = true;
  platform.microphonePcm = false;
  platform.features = buildcfg::buildFeatures();
  platform.fixedGpio = &pins::rp2040Profile();
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
  // Filesystem first: config, settings and palettes all come off it.
  g_storageReady = awtrix::fs::begin();
  if (!g_storageReady) Serial.println("storage: mount failed; persistence unavailable");
  g_cfg = galacticUnicornDefaults();
  if (g_storageReady) {
    g_cfg.load();
    pico::installPaletteLoader();
  }
  if (g_cfg.panelHeight != 8 && g_cfg.panelHeight != 11) {
    Serial.println("warning: Galactic Unicorn panelHeight must be 8 or 11; using 11");
    g_cfg.panelHeight = 11;
  }
  logbuf::setVerbose(g_cfg.debugMode);
  g_board = &activeBoard(g_cfg);
  g_audio.setTone(g_board->toneSink());
  g_audio.setTrack(g_board->trackSink());
  // Two independent decoders, as on ESP32, permit page transitions with icons.
  static DevicePageIcon pageIcon, pageIconB;
  ApplicationServices services{g_audio, g_display, g_system, g_pageClock, awtrixFontCatalog(),
      &pageIcon, &pageIconB, [](uint8_t brightness) { g_board->setBrightness(brightness); }};
  g_builtinApps.reset(new DefaultApps);
  g_runtime.reset(new ApplicationRuntime(g_board->matrixWidth(), g_board->matrixHeight(), services,
                                          g_builtinApps->registry()));
  if (!g_runtime->ready()) Serial.println("matrix: no frame memory");
  g_board->begin();
  g_display.setScreen(&g_runtime->canvas());
  g_runtime->markSensors(*g_board);
  if (g_storageReady) {
    // bootstrap::restoreUserState without the radio, which these builds leave out.
    nvs::loadSettings(g_runtime->engine().state().settings());
    apporder::load(g_runtime->engine());
    g_runtime->engine().setOrderPersist(apporder::save);
  }
  // The runtime applies neither at boot; the ESP32 entry point sets them the same way.
  g_runtime->engine().state().runtime().tempDecimals = g_cfg.tempDecimals;
  g_runtime->bindOutputs(*g_board, g_settingsDirty);
  g_runtime->engine().state().emit(StateEvent::SettingsChanged);
  g_capabilities = g_runtime->capabilitiesJson(platformDescriptor());
  Serial.printf("boot: AWTRIX NG %s on %s (%dx%d); network pending\n",
                AWTRIX_NG_VERSION, g_board->name(), g_board->matrixWidth(),
                g_board->matrixHeight());
}

void loop() {
  const int64_t now = monotonicMs();
  g_runtime->beginFrame(now);
  // Collapses a burst of settings changes into one write; every write costs flash erase budget.
  if (g_settingsDirty && g_storageReady && !g_system.rebootPending && g_settingsSaver.due(now)) {
    nvs::saveSettings(g_runtime->engine().state().settings());
    g_settingsDirty = false;
    g_settingsSaver.saved(now);
  }
  g_runtime->tick(now); // M6a: the script host.
  g_runtime->render(now);
  g_board->show(g_runtime->canvas());
  if (g_system.rebootPending && !g_runtime->powerBusy()) {
    // A requested reboot keeps pending settings, unless a reset just removed them.
    if (g_settingsDirty && g_storageReady && !g_system.resetPending)
      nvs::saveSettings(g_runtime->engine().state().settings());
    rp2040.reboot();
  }
  paceFrame();
}
