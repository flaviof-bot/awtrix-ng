#include <unity.h>

#include "core/input/ButtonWebhook.h"

using awtrix::input::webhookPressBody;
using awtrix::input::webhookTurnBody;

void setUp() {}
void tearDown() {}

static void test_body_matches_the_documented_shape() {
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"left\",\"state\":true,\"uid\":\"dcda0c29dcb8\"}",
                           webhookPressBody("left", true, "dcda0c29dcb8").c_str());
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"middle\",\"state\":false,\"uid\":\"\"}",
                           webhookPressBody("middle", false, "").c_str());
}

static void test_uid_is_escaped() {
  TEST_ASSERT_EQUAL_STRING("{\"button\":\"right\",\"state\":true,\"uid\":\"a\\\"b\"}",
                           webhookPressBody("right", true, "a\"b").c_str());
}

// Escaping changes nothing for a real uid: the bodies are byte for byte 1.2.2's concatenation.
static void test_mac_uid_is_unchanged() {
  const std::string mac = "A0B1C2D3E4F5";
  TEST_ASSERT_EQUAL_STRING(("{\"button\":\"left\",\"state\":false,\"uid\":\"" + mac + "\"}").c_str(),
                           webhookPressBody("left", false, mac).c_str());
  TEST_ASSERT_EQUAL_STRING(("{\"button\":\"knob\",\"turn\":-2,\"uid\":\"" + mac + "\"}").c_str(),
                           webhookTurnBody(-2, mac).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_body_matches_the_documented_shape);
  RUN_TEST(test_uid_is_escaped);
  RUN_TEST(test_mac_uid_is_unchanged);
  return UNITY_END();
}
