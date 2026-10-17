#include <unity.h>

#include "persistence/SystemConfigApply.h"
#include "platform/rp2040/Rp2040Profile.h"

using namespace awtrix;

void setUp() {}
void tearDown() {}

// The Galactic Unicorn's panel and pins are soldered: their keys are not offered.
static void offers_no_panel_or_pin_keys() {
  for (const char* key : {"pinMatrix", "pinBuzzer", "pinI2cSda", "pinI2sBclk",
                          "panelWidth", "panels"})
    TEST_ASSERT_FALSE_MESSAGE(PlatformConfig::offers(key), key);
  for (const char* key : {"mqttHost", "hostname", "authEnabled"})
    TEST_ASSERT_TRUE_MESSAGE(PlatformConfig::offers(key), key);
  TEST_ASSERT_EQUAL_INT(-1, DeviceConfig{}.pinMatrix);
  TEST_ASSERT_EQUAL_INT(28, DeviceConfig{}.pinLdr);
}

// R24: an edit of the pins or the panel changes nothing, even with values no chip accepts.
static void refuses_pin_and_panel_edits() {
  DeviceConfig config;
  const DeviceConfig before = config;
  sysconfig::ApplyError error;
  int applied = 0;
  constexpr auto edit = R"({"pinMatrix":99,"pinBuzzer":5,"pinBtnLeft":-1,"panelWidth":7,"panels":9,"mqttHost":"broker.test"})";
  TEST_ASSERT_TRUE_MESSAGE(sysconfig::apply(config, api::JsonReader(edit), applied, error),
                           error.message.c_str());
  TEST_ASSERT_EQUAL_INT(1, applied);
  TEST_ASSERT_EQUAL_STRING("broker.test", config.mqttHost.c_str());
  TEST_ASSERT_EQUAL_INT(before.pinMatrix, config.pinMatrix);
  TEST_ASSERT_EQUAL_INT(before.pinBuzzer, config.pinBuzzer);
  TEST_ASSERT_EQUAL_INT(before.pinBtnLeft, config.pinBtnLeft);
  TEST_ASSERT_EQUAL_INT(before.panelWidth, config.panelWidth);
  TEST_ASSERT_EQUAL_INT(before.panels, config.panels);
  // The board's own pins still validate; anything else is refused by the fixed profile.
  std::string pinErr;
  TEST_ASSERT_TRUE(config.validatePins(pinErr));
  config.pinBuzzer = 5;
  TEST_ASSERT_FALSE(config.validatePins(pinErr));
  TEST_ASSERT_EQUAL_STRING("Galactic Unicorn (Pico W / Pico 2 W): pin assignments are fixed", pinErr.c_str());
}

// R23: a backup made on an ESP32 restores everything but the ESP32's pins and panel.
static void restores_an_esp32_backup_with_its_own_wiring() {
  DeviceConfig config;
  const DeviceConfig before = config;
  constexpr auto backup = R"({"pinMatrix":32,"pinBtnLeft":26,"pinBtnSelect":27,"pinBtnRight":14,"pinBattery":34,"pinLdr":35,"pinBuzzer":15,"pinI2cSda":21,"pinI2cScl":22,"panelWidth":32,"panels":2,"mqttHost":"esp.broker","mqttEnabled":true})";
  sysconfig::ApplyError error;
  int applied = 0;
  TEST_ASSERT_TRUE_MESSAGE(sysconfig::apply(config, api::JsonReader(backup), applied, error,
                                            sysconfig::Origin::Restore), error.message.c_str());
  TEST_ASSERT_TRUE(config.mqttEnabled);
  TEST_ASSERT_EQUAL_STRING("esp.broker", config.mqttHost.c_str());
  TEST_ASSERT_EQUAL_INT(before.pinMatrix, config.pinMatrix);
  TEST_ASSERT_EQUAL_INT(before.pinLdr, config.pinLdr);
  TEST_ASSERT_EQUAL_INT(before.pinI2cSda, config.pinI2cSda);
  TEST_ASSERT_EQUAL_INT(before.panels, config.panels);
  std::string pinErr;
  TEST_ASSERT_TRUE(config.validatePins(pinErr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(offers_no_panel_or_pin_keys);
  RUN_TEST(refuses_pin_and_panel_edits);
  RUN_TEST(restores_an_esp32_backup_with_its_own_wiring);
  return UNITY_END();
}
