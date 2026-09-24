#include <unity.h>
#include <string>
#include "platform/rp2040/WifiCompat.h"
#include "core/net/HostName.h"

namespace {
struct Wifi {
  enum Mode { Sta, ApSta } current = Sta;
  int ip = 0, dns = 0, gateway = 0, subnet = 0, dns2 = 0;
  int joins = 0, apTearDowns = 0;
  std::string ssid, password;
  void config(int i, int d, int g, int s) { ip = i; dns = d; gateway = g; subnet = s; }
  void setDNS(int d, int d2) { dns = d; dns2 = d2; }
  void mode(Mode m) { current = m; }
  void begin(const char* s, const char* p) {
    // Pinned arduino-pico _beginInternal: end() unless AP_STA, then sets STA.
    if (current != ApSta) ++apTearDowns;
    ++joins; ssid = s; password = p; current = Sta;
  }
};
void static_ip_uses_pico_order_and_both_dns_servers() {
  Wifi w;
  awtrix::platform::pico::configureStatic(w, 10, 20, 30, 40, 50);
  TEST_ASSERT_EQUAL(10, w.ip); TEST_ASSERT_EQUAL(20, w.gateway);
  TEST_ASSERT_EQUAL(30, w.subnet); TEST_ASSERT_EQUAL(40, w.dns);
  TEST_ASSERT_EQUAL(50, w.dns2);
}
void repeated_ap_retries_preserve_portal_mode_and_credentials() {
  Wifi w;
  for (int i = 0; i < 3; ++i) {
    awtrix::platform::pico::join(w, Wifi::ApSta, "test-network", "test-password");
    TEST_ASSERT_EQUAL(Wifi::ApSta, w.current);
  }
  TEST_ASSERT_EQUAL(3, w.joins); TEST_ASSERT_EQUAL(0, w.apTearDowns);
  TEST_ASSERT_EQUAL_STRING("test-network", w.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("test-password", w.password.c_str());
}
void station_reconnect_uses_stored_credentials() {
  Wifi w;
  awtrix::platform::pico::join(w, Wifi::Sta, "saved", "");
  TEST_ASSERT_EQUAL(Wifi::Sta, w.current);
  TEST_ASSERT_EQUAL_STRING("saved", w.ssid.c_str());
  TEST_ASSERT_TRUE(w.password.empty());
}
void default_ap_and_mdns_share_mac_suffix() {
  const auto name = awtrix::net::effectiveHostname("", "00:11:22:AA:BB:CC");
  TEST_ASSERT_EQUAL_STRING("awtrixng-aabbcc", name.c_str());
  TEST_ASSERT_EQUAL_STRING("kitchen", awtrix::net::effectiveHostname("kitchen", "00:11:22:AA:BB:CC").c_str());
}
}
void setUp() {}
void tearDown() {}
int main() {
  UNITY_BEGIN();
  RUN_TEST(static_ip_uses_pico_order_and_both_dns_servers);
  RUN_TEST(repeated_ap_retries_preserve_portal_mode_and_credentials);
  RUN_TEST(station_reconnect_uses_stored_credentials);
  RUN_TEST(default_ap_and_mdns_share_mac_suffix);
  return UNITY_END();
}
