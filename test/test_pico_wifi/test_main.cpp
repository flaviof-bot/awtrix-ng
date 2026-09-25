#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "platform/rp2040/WifiCompat.h"
#include "core/net/HostName.h"

namespace {
struct Ap { std::string ssid; int rssi; uint8_t bssid[6]; };
struct Wifi {
  enum Mode { Sta, ApSta } current = Sta;
  int ip = 0, dns = 0, gateway = 0, subnet = 0, dns2 = 0;
  int joins = 0, apTearDowns = 0, scans = 0, scanDeletes = 0;
  std::string ssid, password;
  std::vector<Ap> air;             // what a scan finds
  const uint8_t* pinned = nullptr;  // BSSID passed to the last begin()
  uint8_t pinnedCopy[6] = {};
  void config(int i, int d, int g, int s) { ip = i; dns = d; gateway = g; subnet = s; }
  void setDNS(int d, int d2) { dns = d; dns2 = d2; }
  void mode(Mode m) { current = m; }
  int scanNetworks(bool async) { TEST_ASSERT_FALSE(async); ++scans; return static_cast<int>(air.size()); }
  const char* SSID(uint8_t i) { return air[i].ssid.c_str(); }
  int RSSI(uint8_t i) { return air[i].rssi; }
  uint8_t* BSSID(uint8_t i, uint8_t* out) { memcpy(out, air[i].bssid, 6); return out; }
  void scanDelete() { ++scanDeletes; }
  void begin(const char* s, const char* p, const uint8_t* bssid = nullptr) {
    // Pinned arduino-pico _beginInternal: end() unless AP_STA, then sets STA.
    if (current != ApSta) ++apTearDowns;
    ++joins; ssid = s; password = p; current = Sta;
    pinned = bssid;
    if (bssid) memcpy(pinnedCopy, bssid, 6);
  }
};
Ap ap(const char* ssid, int rssi, uint8_t last) { return Ap{ssid, rssi, {2, 0, 0, 0, 0, last}}; }
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
void join_pins_the_strongest_bssid_of_the_same_ssid() {
  Wifi w;
  w.air = {ap("home", -86, 1), ap("neighbour", -40, 2), ap("home", -52, 3), ap("home", -70, 4)};
  const auto r = awtrix::platform::pico::join(w, Wifi::Sta, "home", "pw");
  TEST_ASSERT_TRUE(r.pinned);
  TEST_ASSERT_EQUAL(-52, r.rssi);
  TEST_ASSERT_NOT_NULL(w.pinned);
  TEST_ASSERT_EQUAL(3, w.pinnedCopy[5]);  // not the far AP, not the stronger foreign SSID
  TEST_ASSERT_EQUAL(1, w.scans); TEST_ASSERT_EQUAL(1, w.scanDeletes);
}
void join_falls_back_to_unpinned_when_the_ssid_is_not_seen() {
  Wifi w;
  w.air = {ap("neighbour", -40, 2)};  // hidden or out of range
  const auto r = awtrix::platform::pico::join(w, Wifi::Sta, "home", "pw");
  TEST_ASSERT_FALSE(r.pinned);
  TEST_ASSERT_NULL(w.pinned);
  TEST_ASSERT_EQUAL(1, w.joins);
  TEST_ASSERT_EQUAL_STRING("home", w.ssid.c_str());
}
void unpinned_join_skips_the_scan() {
  Wifi w;
  w.air = {ap("home", -52, 3)};
  const auto r = awtrix::platform::pico::join(w, Wifi::Sta, "home", "pw", false);
  TEST_ASSERT_FALSE(r.pinned);
  TEST_ASSERT_EQUAL(0, w.scans);
  TEST_ASSERT_NULL(w.pinned);
}
void ssid_match_is_exact() {
  Wifi w;
  w.air = {ap("home-guest", -30, 5), ap("hom", -30, 6)};
  TEST_ASSERT_FALSE(awtrix::platform::pico::join(w, Wifi::Sta, "home", "pw").pinned);
}
void a_join_in_progress_is_not_restarted_before_its_timeout() {
  using awtrix::platform::pico::joinDue;
  TEST_ASSERT_TRUE(joinDue(0, 0, false, 15000));          // never joined: go
  TEST_ASSERT_FALSE(joinDue(5000, 0, true, 15000));        // the old bug: every 5 s check restarted it
  TEST_ASSERT_FALSE(joinDue(14999, 0, true, 15000));
  TEST_ASSERT_TRUE(joinDue(15000, 0, true, 15000));
  TEST_ASSERT_FALSE(joinDue(5000, 0xFFFFF000u, true, 15000));  // millis() wrapped: 9096 ms elapsed
  TEST_ASSERT_TRUE(joinDue(12000, 0xFFFFF000u, true, 15000));  // 16096 ms elapsed
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
  RUN_TEST(join_pins_the_strongest_bssid_of_the_same_ssid);
  RUN_TEST(join_falls_back_to_unpinned_when_the_ssid_is_not_seen);
  RUN_TEST(unpinned_join_skips_the_scan);
  RUN_TEST(ssid_match_is_exact);
  RUN_TEST(a_join_in_progress_is_not_restarted_before_its_timeout);
  RUN_TEST(default_ap_and_mdns_share_mac_suffix);
  return UNITY_END();
}
