#include <unity.h>
#include <cstdio>
#include "core/PinRules.h"
#include "core/PlatformDescriptor.h"
#include "core/SocProfileJson.h"
#include "core/api/ApiRouter.h"
#include "core/api/CapabilitiesJson.h"
#include "platform/BuildFeatures.h"
#include "platform/rp2040/Rp2040Profile.h"
using namespace awtrix;
void setUp() {}
void tearDown() {}
static constexpr FeatureSet pico{false, false, false, false, false};

static void http_profiles() {
  struct Request { const char* method; const char* path; const char* body; };
  // Dedicated routes answer from the path alone, so even a body no route would accept gets 503.
  const Request requests[] = {
      {"GET", "/api/v1/scripts/shared", ""},
      {"GET", "/api/v1/apps/script/demo", ""},
      {"PUT", "/api/v1/apps/script/demo", "print(1)"},
      {"DELETE", "/api/v1/apps/script/demo", ""},
      {"PUT", "/api/v1/apps/script-update/demo", "{}"},
      {"GET", "/api/v1/apps/demo/config", ""},
      {"PATCH", "/api/v1/apps/demo/config", "{}"},
      {"PATCH", "/api/v1/apps/demo/data", "{}"},
      {"GET", "/api/v1/audio/mp3", ""},
      {"POST", "/api/v1/audio/mp3", "binary"},
      {"DELETE", "/api/v1/audio/mp3/demo.mp3", ""},
      {"GET", "/api/v1/audio/stations", ""},
      {"PUT", "/api/v1/audio/stations", "{\"stations\":[]}"},
      {"PUT", "/api/v1/audio/stations", "not json"},
      {"POST", "/api/v1/audio/play", "{\"file\":\"demo\"}"},
      {"POST", "/api/v1/audio/play", "{\"station\":\"demo\"}"},
      {"POST", "/api/v1/audio/play", "{\"station\":0}"},
      {"POST", "/api/v1/audio/play", "{\"file\":\"https://example.com/live.mp3\"}"},
      {"POST", "/api/v1/audio/play", "[{\"file\":\"demo\"},{\"file\":\"https://example.com/a.mp3\"}]"},
      {"GET", "/update", ""}, {"POST", "/update", "binary"},
  };
  for (const auto& req : requests) {
    Command cmd;
    api::HttpResult result;
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(api::RouteOutcome::Respond),
        static_cast<int>(api::routeHttp(req.method, req.path, req.body, cmd, result, pico)), req.body);
    TEST_ASSERT_EQUAL_INT_MESSAGE(503, result.status, req.body);
    TEST_ASSERT_TRUE(result.body.find("\"code\":\"unavailable\"") != std::string::npos);
    // Every board that keeps every feature routes exactly as before.
    api::HttpResult baseline, explicitEsp;
    Command original;
    const auto before = api::routeHttp(req.method, req.path, req.body, original, baseline);
    const auto after = api::routeHttp(req.method, req.path, req.body, cmd, explicitEsp, FeatureSet{});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(before), static_cast<int>(after));
    TEST_ASSERT_EQUAL_INT(baseline.status, explicitEsp.status);
    TEST_ASSERT_EQUAL_STRING(baseline.body.c_str(), explicitEsp.body.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(original.type), static_cast<int>(cmd.type));
    TEST_ASSERT_NOT_EQUAL(503, explicitEsp.status);
  }
  // What the Pico does have keeps working on the shared routes.
  const Request supported[] = {
      {"POST", "/api/v1/device/reboot", ""},
      {"POST", "/api/v1/audio/play", "{\"rtttl\":\"x:d=4,o=5,b=100:c\"}"},
      {"POST", "/api/v1/audio/play", "[{\"file\":\"demo\"},{\"rtttl\":\"x:d=4,o=5,b=100:c\"}]"},
      {"PATCH", "/api/v1/apps/builtin/Time/config", "{}"},
  };
  for (const auto& req : supported) {
    Command cmd;
    api::HttpResult result;
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(api::RouteOutcome::Routed),
        static_cast<int>(api::routeHttp(req.method, req.path, req.body, cmd, result, pico)), req.body);
  }
  // A shared route parses before it decides: a malformed body is still the client's mistake.
  Command cmd;
  api::HttpResult result;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
      static_cast<int>(api::routeHttp("POST", "/api/v1/audio/play", "{", cmd, result, pico)));
  TEST_ASSERT_EQUAL_INT(400, result.status);
}

static void mqtt_profiles() {
  const char* payloads[] = {"{\"file\":\"demo\"}", "{\"station\":\"demo\"}",
      "{\"station\":0}", "{\"file\":\"https://example.com/live.mp3\"}"};
  for (const auto payload : payloads) {
    Command cmd;
    std::string result, body = payload;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
        static_cast<int>(api::routeMqtt("cmd/audio/play", body, cmd, result, pico)));
    TEST_ASSERT_EQUAL_STRING(api::mqttResult(DispatchResult::Unavailable, {}).c_str(), result.c_str());
    body = payload;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
        static_cast<int>(api::routeMqtt("cmd/audio/play", body, cmd, result, FeatureSet{})));
  }
  Command cmd;
  std::string result, body = "{}";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
      static_cast<int>(api::routeMqtt("cmd/audio/stations", body, cmd, result, pico)));
  TEST_ASSERT_EQUAL_STRING(api::mqttResult(DispatchResult::Unavailable, {}).c_str(), result.c_str());
  TEST_ASSERT_TRUE(api::isResultEcho("cmd/audio/stations/result"));
  body = "{\"rtttl\":\"x:d=4,o=5,b=100:c\"}";
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
      static_cast<int>(api::routeMqtt("cmd/audio/play", body, cmd, result, pico)));
  FeatureSet noTls;
  noTls.outboundTls = false;
  api::HttpResult response;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Respond),
      static_cast<int>(api::routeHttp("POST", "/api/v1/audio/play",
          "{\"file\":\"https://example.com/live.mp3\"}", cmd, response, noTls)));
  TEST_ASSERT_EQUAL_INT(503, response.status);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(api::RouteOutcome::Routed),
      static_cast<int>(api::routeHttp("POST", "/api/v1/audio/play",
          "{\"file\":\"http://example.com/live.mp3\"}", cmd, response, noTls)));
}

static void fixed_pins() {
  const auto& profile = pins::rp2040Profile();
  std::string error;
  TEST_ASSERT_TRUE(pins::validate(profile.defaults, profile, error));
  int pins::PinSet::* const fields[] = {&pins::PinSet::matrix, &pins::PinSet::btnLeft,
      &pins::PinSet::btnSelect, &pins::PinSet::btnRight, &pins::PinSet::battery,
      &pins::PinSet::ldr, &pins::PinSet::buzzer, &pins::PinSet::i2cSda,
      &pins::PinSet::i2cScl, &pins::PinSet::dfRx, &pins::PinSet::dfTx,
      &pins::PinSet::i2sBclk, &pins::PinSet::i2sLrclk, &pins::PinSet::i2sDout,
      &pins::PinSet::i2sMclk, &pins::PinSet::ampEnable};
  for (auto field : fields) {
    auto changed = profile.defaults;
    ++(changed.*field);
    TEST_ASSERT_FALSE(pins::validate(changed, profile, error));
    TEST_ASSERT_EQUAL_STRING("Galactic Unicorn (Pico W / Pico 2 W): pin assignments are fixed", error.c_str());
  }
  auto changed = profile.defaults;
  changed.dfplayerEnabled = true;
  TEST_ASSERT_FALSE(pins::validate(changed, profile, error));
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.i2cSda);
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.i2cScl);
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.battery);
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.buzzer);
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.dfRx);
  TEST_ASSERT_EQUAL_INT(-1, profile.defaults.dfTx);
  TEST_ASSERT_TRUE(pins::validate(pins::esp32Profile().defaults, pins::esp32Profile(), error));
  TEST_ASSERT_TRUE(pins::validate(pins::esp32s3Profile().defaults, pins::esp32s3Profile(), error));
}

static void capabilities() {
  sound::AudioRouter audio;
  PlatformDescriptor platform{"rp2040", {53, 11, false}, false};
  platform.features = pico;
  platform.fixedGpio = &pins::rp2040Profile();
  const auto json = api::capabilitiesJson({}, {}, {}, audio.caps(), &platform);
  TEST_ASSERT_TRUE(json.find("\"scripting\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"scriptUpdates\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"mp3\":false") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"radio\":false") != std::string::npos);
  TEST_ASSERT_TRUE(api::capabilitiesJson({}, {}, {}, audio.caps()).find("\"scripting\":true") != std::string::npos);
  // The pins are the board's, and the web UI shows them rather than offering them.
  TEST_ASSERT_TRUE(json.find("\"label\":\"Galactic Unicorn (Pico W / Pico 2 W)\",\"fixed\":true,") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"pinI2cSda\":-1,\"pinI2cScl\":-1,") != std::string::npos);
  TEST_ASSERT_TRUE(pins::toJson(pins::esp32Profile()).find("\"label\":\"ESP32\",\"fixed\":false,") != std::string::npos);
  TEST_ASSERT_TRUE(pins::toJson(pins::esp32s3Profile()).find("\"fixed\":false,") != std::string::npos);
  // A host without GPIO keeps gpio:null; only a board with soldered wiring reports it.
  PlatformDescriptor desktop{"linux", {32, 8, true}, false};
  const auto host = api::capabilitiesJson({}, {}, {}, audio.caps(), &desktop);
  TEST_ASSERT_TRUE(host.find("\"gpio\":null") != std::string::npos);
  TEST_ASSERT_TRUE(host.find("\"scriptUpdates\":true,\"scripting\":true") != std::string::npos);
  TEST_ASSERT_TRUE(buildcfg::buildFeatures().all());
  std::printf("PICO_CAPABILITIES=%s\n", json.c_str());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(http_profiles);
  RUN_TEST(mqtt_profiles);
  RUN_TEST(fixed_pins);
  RUN_TEST(capabilities);
  return UNITY_END();
}
