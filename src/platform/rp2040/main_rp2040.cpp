#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include <memory>
#include <string>

#include "AppConfig.h"
#include "core/ApplicationRuntime.h"
#include "core/DeviceCapabilities.h"
#include "core/FrameClock.h"
#include "core/PlatformDescriptor.h"
#include "core/ScreenDisplay.h"
#include "core/SystemPageClock.h"
#include "core/SettingsSaver.h"
#include "core/apps/DefaultApps.h"
#include "core/effects/EffectNoise.h"
#include "core/input/ButtonWebhook.h"
#include "core/render/BootScreen.h"
#include "core/render/PaletteStore.h"
#include "core/render/ProvisioningScreen.h"
#include "core/render/TextRenderer.h"
#include "hal/BoardRegistry.h"
#include "hal/GalacticUnicornBoard.h"
#include "hal/IBoard.h"
#include "media/AwtrixFontAdapter.h"
#include "media/DevicePageIcon.h"
#include "persistence/AppOrderStore.h"
#include "persistence/Filesystem.h"
#include "persistence/NvsSettings.h"
#include "persistence/SystemConfigApply.h"
#include "platform/BuildFeatures.h"
#include "platform/rp2040/PaletteFiles.h"
#include "platform/rp2040/PicoSleep.h"
#include "platform/rp2040/RadioStartup.h"
#include "platform/rp2040/ResetReason.h"
#include "platform/rp2040/Rp2040Profile.h"
#include "platform/rp2040/TimeService.h"
#include "system/Log.h"
#include "system/Watchdog.h"
#include "system/GalacticUnicornControls.h"
#include "transport/net/ArtnetService.h"
#include "transport/net/DiscoveryService.h"
#include "transport/http/HttpApiServer.h"
#include "transport/net/NetworkService.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <hardware/gpio.h>
#include <pico/rand.h>
#include "system/MonotonicClock.h"

// Feature macros describe this build, not a claim that a runtime service exists.
static_assert(!AWTRIX_FEATURE_SCRIPTING && !AWTRIX_FEATURE_MP3 &&
              !AWTRIX_FEATURE_RADIO && !AWTRIX_FEATURE_OUTBOUND_TLS &&
              !AWTRIX_FEATURE_BROWSER_OTA, "Pico build must not enable unsupported services");

// Build-only measurement switch (-D AWTRIX_PICO_UDP=0); every release build has both UDP services.
#ifndef AWTRIX_PICO_UDP
#define AWTRIX_PICO_UDP 1
#endif

using namespace awtrix;

namespace {
// The Pico has no deep sleep: a timed sleep blanks the panel, lets the radio save power and waits
// for the duration or a new press of the Sleep key, then restarts like an ESP32 waking up.
class PicoSystem final : public ISystemService {
 public:
  void reboot() override { rebootPending = true; }
  void sleep(uint64_t durationMs) override {
    if (durationMs == 0) return;
    sleepMs = durationMs;
    sleepPending = true;
    rebootPending = true;
  }
  // A reset removes the namespace files and reboots without saving over them.
  void factoryReset() override {
    watchdog::stop();
    LittleFS.remove("/NVS/awtrix-cfg.bin");
    resetSettings();
  }
  void resetSettings() override {
    LittleFS.remove("/NVS/awtrix-ng.bin");
    resetPending = true;
    rebootPending = true;
  }
  // Runs once the power animation has played out; never returns.
  [[noreturn]] void restart(IBoard& board, Canvas& canvas) {
    if (sleepPending) {
      canvas.clear(0x000000u);
      board.show(canvas);
      delay(200);
      WiFi.aggressiveLowPowerMode();
      const auto pressed = [] { return !gpio_get(galactic::Sleep); };
      PicoSleep sleeper(monotonicMs(), sleepMs, pressed());
      while (!sleeper.wake(monotonicMs(), pressed())) {
        watchdog::feed();
        delay(10);
      }
    }
    rp2040.reboot();
    for (;;) {}
  }
  bool rebootPending = false;
  bool resetPending = false;
  bool sleepPending = false;
  uint64_t sleepMs = 0;
};

// Eight rows is the compatibility mode; 9 and 10 are rejected by ConfigRules.
constexpr DisplayLimits kUnicornDisplayLimits{53, 53, 8, 11, 53 * 11};

IBoard* g_board = nullptr;
GalacticUnicornControls g_controls;
std::string g_uid;

// The buttonCallback webhook, from the loop like an ESP32's (system/PeripheryService.cpp) and with
// the same body and 300 ms waits, so an unreachable listener only stalls the display briefly.
void postButton(const std::string& url, int button, bool state) {
  WiFiClient wc;
  // arduino-pico's HTTPClient has no connect timeout; the client's own timeout bounds the connect.
  wc.setTimeout(300);
  HTTPClient http;
  http.setTimeout(300);
  if (!http.begin(wc, url.c_str())) return;
  http.addHeader("Content-Type", "application/json");
  const std::string body = input::webhookPressBody(input::kWebhookButtonNames[button], state, g_uid);
  http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body.data())), body.size());
  http.end();
}
sound::AudioRouter g_audio; // Null sinks honestly report MP3/radio unavailable.
ScreenDisplay g_display;
PicoSystem g_system;
SystemPageClock g_pageClock;
platform::TimeService g_time;
bool g_netWasConnected = false;
#if AWTRIX_PICO_UDP
DiscoveryService g_disco;
ArtnetService g_artnet;
#endif
DeviceConfig g_cfg;
bool g_storageReady = false;
bool g_settingsDirty = false;
SettingsSaver g_settingsSaver{-100000};
std::unique_ptr<DefaultApps> g_builtinApps;
std::unique_ptr<ApplicationRuntime> g_runtime;
NetworkService g_net;
HttpApiServer g_http;
std::string g_capabilities; // Served by the HTTP API; M5b: published over MQTT.

// Holding B (select) for one second at boot forces the provisioning AP without erasing the stored
// credentials, as on the ESP32. The runtime hold of B (the launcher) only starts in loop().
bool holdingSelectAtBoot() {
  ButtonState buttons;
  g_board->pollButtons(buttons);
  if (!buttons.select) return false;
  const unsigned long start = millis();
  while (millis() - start < 1000) {
    g_board->pollButtons(buttons);
    if (!buttons.select) return false;
    delay(20);
  }
  Canvas& canvas = g_runtime->canvas();
  canvas.clear(0x000000u);
  text::drawText(canvas, awtrixFont(), 0, 6, "SETUP", 0xFFA000u);
  g_board->show(canvas);
  logf("boot: SELECT held, forcing provisioning AP (credentials kept)");
  return true;
}

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

// As on the ESP32: at boot and after every settings change.
void refreshCapabilities() {
  const PlatformDescriptor platform = platformDescriptor();
  g_capabilities = g_runtime->capabilitiesJson(platform);
  g_http.setCapabilitiesJson(std::make_shared<const std::string>(g_capabilities));
  g_http.setDeviceCapabilities(DeviceCapabilities::from(g_audio.caps(), &platform));
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
  noise::reseed(get_rand_32());
  Serial.printf("reset: %s\n", platform::resetReasonName());
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
  // A pin map or panel size stored by another build would make every PUT /api/v1/system fail.
  sysconfig::adoptFixedBoard(g_cfg);
  logbuf::setVerbose(g_cfg.debugMode);
  g_board = &activeBoard(g_cfg);
  g_audio.setTone(g_board->toneSink());
  g_audio.setTrack(g_board->trackSink());
  // Two independent decoders, as on ESP32, permit page transitions with icons.
  static DevicePageIcon pageIcon, pageIconB;
  ApplicationServices services{g_audio, g_display, g_system, g_pageClock, awtrixFontCatalog(),
      &pageIcon, &pageIconB, [](uint8_t brightness) { g_board->setBrightness(brightness); }};
  services.lightConfig = [] { return g_cfg.lightConfig(); };
  g_builtinApps.reset(new DefaultApps);
  g_runtime.reset(new ApplicationRuntime(g_board->matrixWidth(), g_board->matrixHeight(), services,
                                          g_builtinApps->registry()));
  if (!g_runtime->ready()) Serial.println("matrix: no frame memory");
  g_board->begin();
  g_display.setScreen(&g_runtime->canvas());
  g_runtime->markSensors(*g_board);
  g_controls.begin(g_runtime->engine(), g_cfg, &g_runtime->launcher());
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
  Serial.printf("boot: AWTRIX NG %s on %s (%dx%d); PIO/DMA panel\n",
                AWTRIX_NG_VERSION, g_board->name(), g_board->matrixWidth(),
                g_board->matrixHeight());
  const int64_t bootT0 = monotonicMs();
  auto showBootLogo = [bootT0] {
    watchdog::feed();
    render::drawBootLogo(g_runtime->canvas(), awtrixFont(), bootT0, monotonicMs());
    g_board->show(g_runtime->canvas());
  };
  showBootLogo();
  const bool forceAp = holdingSelectAtBoot();
  // The radio takes its PIO state machine only now, after the panel owns its PIO and DMA.
  platform::beginRadio();
  watchdog::begin(); // filesystem mount and radio startup are complete
  // Joining a network from the provisioning AP reboots, so setup reruns with an address.
  g_net.setOnJoinedFromAp([] { g_system.reboot(); });
  g_net.begin(g_cfg, g_runtime->engine().state().runtime().wifi, forceAp, showBootLogo);
  g_time.apply(g_cfg.tz, g_cfg.ntpServer, true);
  g_netWasConnected = g_net.isConnected();
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  g_uid = mac.c_str();
  g_controls.setButtonPost(postButton);
  const uint16_t webPort =
      g_net.apMode() ? 80 : (g_cfg.webPort > 0 ? static_cast<uint16_t>(g_cfg.webPort) : 80);
  // In AP mode the server admits only the provisioning routes (core/ProvisioningPolicy.h).
  g_http.begin(webPort, g_runtime->engine(), *g_board, g_runtime->canvas(), mac.c_str(), g_cfg,
               g_net.apMode());
  g_http.setOnConfigChanged([] {
    // The panel size is fixed, so only mirror and rotate reach the board, and they apply live.
    const MatrixLayout layout = g_cfg.matrixLayout();
    if (layout.width() == g_board->matrixWidth() && layout.height() == g_board->matrixHeight())
      g_board->setMatrixLayout(layout);
    refreshCapabilities();
    g_runtime->engine().state().runtime().tempDecimals = g_cfg.tempDecimals;
    logbuf::setVerbose(g_cfg.debugMode);
  });
  g_http.setOnAssetsChanged([] {
    g_runtime->invalidateIcons();
    render::clearPaletteCache();
  });
  refreshCapabilities();
#if AWTRIX_PICO_UDP
  if (g_netWasConnected) {
    g_disco.begin(g_net.hostname(), g_cfg.webPort);
    if (g_cfg.artnet) g_artnet.begin();
  }
#endif
  static_cast<GalacticUnicornBoard*>(g_board)->logRefreshProgress();
}

void loop() {
  watchdog::feed();
  const int64_t now = monotonicMs();
  auto& unicorn = static_cast<GalacticUnicornBoard&>(*g_board);
  g_controls.tick(g_runtime->engine(), unicorn.readInputs(), now);
  g_controls.sampleLight(g_runtime->engine(), unicorn.readLdrRaw(), now);
  g_runtime->beginFrame(now);
  g_net.tick(static_cast<uint32_t>(now));
  g_http.tick();
  const bool netConnected = g_net.isConnected();
  // A changed tz or ntpServer restarts SNTP; so does the link coming back, as on the ESP32.
  g_time.apply(g_cfg.tz, g_cfg.ntpServer, netConnected && !g_netWasConnected);
#if AWTRIX_PICO_UDP
  if (netConnected && !g_netWasConnected) g_disco.begin(g_net.hostname(), g_cfg.webPort);
  if (netConnected && g_cfg.artnet) g_artnet.begin();
  else g_artnet.end();
  if (netConnected) g_disco.tick();
#endif
  g_netWasConnected = netConnected;
  // Collapses a burst of settings changes into one write; every write costs flash erase budget.
  if (g_settingsDirty && g_storageReady && !g_system.rebootPending && g_settingsSaver.due(now)) {
    nvs::saveSettings(g_runtime->engine().state().settings());
    g_settingsDirty = false;
    g_settingsSaver.saved(now);
  }
  g_runtime->tick(now); // M6a: the script host.
  const bool artnetFrame = g_runtime->render(now, [](Canvas& canvas, int64_t frameMs, void*) {
    if (g_net.apMode()) {
      render::drawProvisioningScreen(canvas, awtrixFont(), frameMs);
      return PlatformFrame::Rendered;
    }
#if AWTRIX_PICO_UDP
    return g_artnet.tick(canvas, frameMs) ? PlatformFrame::ExternalRate : PlatformFrame::None;
#else
    return PlatformFrame::None;
#endif
  });
  g_board->show(g_runtime->canvas());
  if (g_system.rebootPending && !g_runtime->powerBusy()) {
    // A requested reboot or sleep keeps pending settings, unless a reset just removed them.
    if (g_settingsDirty && g_storageReady && !g_system.resetPending)
      nvs::saveSettings(g_runtime->engine().state().settings());
    g_system.restart(*g_board, g_runtime->canvas());
  }
  // An Art-Net sender sets its own frame rate, as on the ESP32.
  if (artnetFrame) {
    delay(5);
  } else {
    paceFrame();
  }
}
