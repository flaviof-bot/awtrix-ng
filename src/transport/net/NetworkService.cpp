#include "transport/net/NetworkService.h"

#include <WiFi.h>
#if defined(AWTRIX_PLATFORM_RP2040)
#include <LEAmDNS.h>
#include <pico/cyw43_arch.h>
#include "platform/rp2040/WifiCompat.h"
#else
#include <ESPmDNS.h>
#include <esp_wifi.h>
#endif

#include <cstring>

#include "core/net/HostName.h"
#include "core/net/WifiLink.h"
#include "system/Log.h"

namespace awtrix {

namespace {
IPAddress parseIp(const std::string& s) {
  IPAddress a;
  a.fromString(s.c_str());
  return a;
}
constexpr unsigned long kApRetryMs = 30000;
constexpr unsigned long kCheckMs = 5000;
constexpr int kWeakChecksBeforeRoam = 6;
constexpr unsigned long kRoamCooldownMs = 300000;

unsigned long joinTimeoutMs(const DeviceConfig& cfg) {
  return cfg.wifiConnectTimeout > 0 ? static_cast<unsigned long>(cfg.wifiConnectTimeout) : 15000UL;
}

#if defined(AWTRIX_PLATFORM_RP2040)
// Pico join bookkeeping. A pinned join (strongest BSSID from a scan) that fails is followed by an
// unpinned one, so a scan quirk or a hidden SSID can never lock the device out.
unsigned long lastJoinMs = 0;
bool joinedBefore = false;
bool pinNextJoin = true;
platform::pico::JoinResult lastJoin;
// CYW43 has one radio: while the provisioning AP is up the station can only join on the AP's
// channel, so a join from AP_STA fails unless the router happens to use it. The Pico drops the AP
// for one join window instead, and less often than ESP32 retries.
constexpr unsigned long kPicoApRetryMs = 60000;

const char* cyw43LinkName(int link) {
  switch (link) {
    case CYW43_LINK_DOWN:    return "down";
    case CYW43_LINK_JOIN:    return "joining";
    case CYW43_LINK_NOIP:    return "no IP";
    case CYW43_LINK_UP:      return "up";
    case CYW43_LINK_FAIL:    return "failed";
    case CYW43_LINK_NONET:   return "network not found";
    case CYW43_LINK_BADAUTH: return "authentication rejected";
    default:                 return "unknown";
  }
}

// A pinned join (strongest BSSID) that has not connected after half the timeout gets the other half
// as an unpinned join, so an AP that refuses the Pico can never cost the whole attempt.
bool pinnedJoinStalled(unsigned long nowMs, unsigned long timeoutMs) {
  return lastJoin.pinned && WiFi.status() != WL_CONNECTED && nowMs - lastJoinMs >= timeoutMs / 2;
}

void logPinnedFailure() {
  const int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
  const uint8_t* b = lastJoin.bssid;
  logf("wifi: %02x:%02x:%02x:%02x:%02x:%02x did not accept the join (cyw43 link %d, %s); "
       "letting the radio choose", b[0], b[1], b[2], b[3], b[4], b[5], link, cyw43LinkName(link));
}
#endif

void joinStation(const DeviceConfig& cfg, bool apMode) {
#if defined(AWTRIX_PLATFORM_RP2040)
  const auto r = platform::pico::join(WiFi, apMode ? WIFI_AP_STA : WIFI_STA,
                                      cfg.wifiSsid.c_str(), cfg.wifiPass.c_str(), pinNextJoin);
  lastJoinMs = millis();
  joinedBefore = true;
  lastJoin = r;
  if (r.pinned)
    logf("wifi: joining \"%s\" via %02x:%02x:%02x:%02x:%02x:%02x (%d dBm, strongest AP)",
         cfg.wifiSsid.c_str(), r.bssid[0], r.bssid[1], r.bssid[2], r.bssid[3], r.bssid[4],
         r.bssid[5], r.rssi);
  else
    logf("wifi: joining \"%s\" (%s)", cfg.wifiSsid.c_str(),
         pinNextJoin ? "not seen in scan" : "unpinned retry");
  pinNextJoin = !r.pinned;
#else
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
#endif
}

void reconnectStation(const DeviceConfig& cfg) {
#if defined(AWTRIX_PLATFORM_RP2040)
  // A second begin() would abort a join that is still in progress.
  if (!platform::pico::joinDue(millis(), lastJoinMs, joinedBefore, joinTimeoutMs(cfg))) return;
  joinStation(cfg, false);
#else
  WiFi.reconnect();
#endif
}

net::WifiAssoc assocNow(bool apMode) {
  // In provisioning mode the station side is only ever mid-retry: a successful join restarts the
  // device, so "connected" is not a state the AP branch ever has to report.
  if (apMode) return net::WifiAssoc::Disconnected;
  switch (WiFi.status()) {
    case WL_CONNECTED:      return net::WifiAssoc::Connected;
    case WL_NO_SSID_AVAIL:  return net::WifiAssoc::NoSsidFound;
    case WL_CONNECT_FAILED: return net::WifiAssoc::AuthFailed;
    case WL_IDLE_STATUS:
    case WL_SCAN_COMPLETED: return net::WifiAssoc::Idle;
    default:                return net::WifiAssoc::Disconnected;
  }
}
}

void NetworkService::publishStatus() {
  if (!status_) return;
  const bool hasSsid = cfg_ && !cfg_->wifiSsid.empty();
  net::applyWifiAssoc(*status_, assocNow(apMode_), hasSsid,
                      hasSsid ? cfg_->wifiSsid : std::string(),
                      std::string(WiFi.localIP().toString().c_str()));
}

void NetworkService::begin(const DeviceConfig& cfg, bool forceAp,
                           const std::function<void()>& onWait) {
  hostname_ = net::effectiveHostname(cfg.hostname, WiFi.macAddress().c_str());
  WiFi.persistent(true);
  WiFi.setHostname(hostname_.c_str());
  WiFi.mode(WIFI_STA);
#if !defined(AWTRIX_PLATFORM_RP2040)
  // Widest legal channel set (1-13) so an AP on 12 or 13 is visible; the regulatory domain is
  // corrected from the AP's country IE once we associate.
  wifi_country_t country = {};
  memcpy(country.cc, "CN", 3);
  country.schan = 1;
  country.nchan = 13;
  country.policy = WIFI_COUNTRY_POLICY_AUTO;
  esp_wifi_set_country(&country);
  // Modem sleep adds a hundred milliseconds of latency to every packet, which shows up as stuttery
  // Art-Net and laggy API calls. The device is mains-powered, so trade the power for latency.
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#endif

  if (cfg.netStatic && !cfg.ip.empty()) {
#if defined(AWTRIX_PLATFORM_RP2040)
    platform::pico::configureStatic(WiFi, parseIp(cfg.ip), parseIp(cfg.gateway),
        parseIp(cfg.subnet), cfg.dns1.empty() ? parseIp(cfg.gateway) : parseIp(cfg.dns1),
        cfg.dns2.empty() ? IPAddress(0, 0, 0, 0) : parseIp(cfg.dns2));
#else
    WiFi.config(parseIp(cfg.ip), parseIp(cfg.gateway), parseIp(cfg.subnet),
                cfg.dns1.empty() ? parseIp(cfg.gateway) : parseIp(cfg.dns1),
                cfg.dns2.empty() ? IPAddress(0, 0, 0, 0) : parseIp(cfg.dns2));
#endif
  }

  cfg_ = &cfg;
  const unsigned long timeoutMs = joinTimeoutMs(cfg);
  if (!forceAp && !cfg.wifiSsid.empty()) {
    joinStation(cfg, false);
    if (status_) net::applyWifiAssoc(*status_, net::WifiAssoc::Joining, true, cfg.wifiSsid, "");
    const unsigned long start = millis();
    // Blocks boot until the join succeeds or times out; onWait keeps the boot animation moving so
    // the matrix does not look frozen.
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
#if defined(AWTRIX_PLATFORM_RP2040)
      if (pinnedJoinStalled(millis(), timeoutMs)) {
        logPinnedFailure();
        joinStation(cfg, false);
      }
#endif
      if (onWait) onWait();
      delay(10);
    }
  }

  if (forceAp || WiFi.status() != WL_CONNECTED) {
    apMode_ = true;
    startAp(forceAp ? "forced by button" : "no connection");
  } else {
    apMode_ = false;
    logf("wifi: connected to \"%s\" (%d dBm) as %s", WiFi.SSID().c_str(), WiFi.RSSI(),
         WiFi.localIP().toString().c_str());
#if defined(AWTRIX_PLATFORM_RP2040)
    uint8_t b[6] = {};
    WiFi.BSSID(b);
    logf("wifi: associated with %02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
#endif
    logf("heap: %u KB free with radio up",
#if defined(AWTRIX_PLATFORM_RP2040)
         (unsigned)(rp2040.getFreeHeap() / 1024));
#else
         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024));
#endif
    if (MDNS.begin(hostname_.c_str())) {
      String mac = WiFi.macAddress();
      mac.replace(":", "");
      mac.toLowerCase();
      const uint16_t port = cfg.webPort > 0 ? static_cast<uint16_t>(cfg.webPort) : 80;
      MDNS.addService("http", "tcp", port);
      MDNS.addService("awtrixng", "tcp", port);
      MDNS.addServiceTxt("awtrixng", "tcp", "id", mac.c_str());
      MDNS.addServiceTxt("awtrixng", "tcp", "name", hostname_.c_str());
      MDNS.addServiceTxt("awtrixng", "tcp", "type", "awtrixng");
    }
  }

  publishStatus();
  // The join loop above gives up silently, so nothing else would record why boot ended offline.
  if (status_ && status_->phase == net::LinkPhase::Offline &&
      status_->error == net::LinkError::None && !forceAp)
    status_->setError(net::LinkError::Timeout);
}

void NetworkService::startAp(const char* why) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(hostname_.c_str());
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dns_.start(53, "*", WiFi.softAPIP());
  logf("wifi: %s, provisioning AP \"%s\" at %s (captive portal)", why, hostname_.c_str(),
       WiFi.softAPIP().toString().c_str());
}

void NetworkService::tick() {
#if defined(AWTRIX_PLATFORM_RP2040)
  if (!apMode_) MDNS.update();
#endif
  if (apMode_) {
    if (!apPaused_) dns_.processNextRequest();
    retryJoinFromAp();
    return;
  }
  const unsigned long nowMs = millis();
  if (nowMs - lastCheckMs_ < kCheckMs) return;
  lastCheckMs_ = nowMs;
  publishStatus();
  if (WiFi.status() != WL_CONNECTED) {
    weakChecks_ = 0;
#if defined(AWTRIX_PLATFORM_RP2040)
    // Only log when a new attempt starts; the raw CYW43 link state says why the link is down.
    if (cfg_ && platform::pico::joinDue(nowMs, lastJoinMs, joinedBefore, joinTimeoutMs(*cfg_))) {
      logf("wifi: connection lost (status %d, cyw43 link %d), rejoining",
           static_cast<int>(WiFi.status()), cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA));
      reconnectStation(*cfg_);
    }
#else
    logf("wifi: connection lost, reconnecting");
    if (cfg_) reconnectStation(*cfg_);
#endif
    if (status_) net::noteWifiRetry(*status_, kCheckMs);
    return;
  }
#if defined(AWTRIX_PLATFORM_RP2040)
  pinNextJoin = true;
#endif
  roamIfWeak(nowMs);
}

// Forces a re-association when the signal stays below the configured threshold, since the ESP32
// otherwise clings to a weak AP indefinitely. Requires several bad samples plus a long cooldown so
// a passing dip cannot start flapping.
void NetworkService::roamIfWeak(unsigned long nowMs) {
  if (!cfg_ || cfg_->wifiRoamRssi >= 0) return;
  if (nowMs - lastRoamMs_ < kRoamCooldownMs) return;
  if (WiFi.RSSI() >= cfg_->wifiRoamRssi) {
    weakChecks_ = 0;
    return;
  }
  if (++weakChecks_ < kWeakChecksBeforeRoam) return;
  weakChecks_ = 0;
  lastRoamMs_ = nowMs;
  logf("wifi: %d dBm below the %d dBm roam threshold, looking for a stronger AP",
       static_cast<int>(WiFi.RSSI()), cfg_->wifiRoamRssi);
  reconnectStation(*cfg_);
}

// In provisioning mode, keep trying the stored credentials so the device recovers on its own once
// the router comes back. Skipped while someone is attached to the AP, because a join attempt
// disrupts the portal they are using.
void NetworkService::retryJoinFromAp() {
  if (!cfg_ || cfg_->wifiSsid.empty()) return;
#if defined(AWTRIX_PLATFORM_RP2040)
  const unsigned long nowMs = millis();
  if (apPaused_) {
    if (WiFi.status() == WL_CONNECTED) {
      logf("wifi: joined \"%s\" from provisioning mode, restarting to leave the AP",
           WiFi.SSID().c_str());
      if (onJoinedFromAp_) onJoinedFromAp_();
      return;
    }
    const unsigned long timeoutMs = joinTimeoutMs(*cfg_);
    if (pinnedJoinStalled(nowMs, timeoutMs)) {
      logPinnedFailure();
      joinStation(*cfg_, false);
      return;
    }
    if (nowMs - lastJoinMs < timeoutMs) return;
    apPaused_ = false;
    lastApRetryMs_ = nowMs;
    const int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    logf("wifi: rejoin failed (cyw43 link %d, %s)", link, cyw43LinkName(link));
    startAp("still no connection");
    return;
  }
  if (WiFi.softAPgetStationNum() > 0) return;
  if (nowMs - lastApRetryMs_ < kPicoApRetryMs) return;
  publishStatus();
  if (status_) net::noteWifiRetry(*status_, kPicoApRetryMs);
  logf("wifi: pausing the provisioning AP for a station-only join");
  dns_.stop();
  apPaused_ = true;
  joinStation(*cfg_, false);  // WIFI_STA: arduino-pico's begin() tears the AP down
#else
  if (WiFi.softAPgetStationNum() > 0) return;
  const unsigned long now = millis();
  if (now - lastApRetryMs_ < kApRetryMs) return;
  lastApRetryMs_ = now;
  if (WiFi.status() != WL_CONNECTED) {
    publishStatus();
    if (status_) net::noteWifiRetry(*status_, kApRetryMs);
    joinStation(*cfg_, true);
    return;
  }
  logf("wifi: joined \"%s\" from provisioning mode, restarting to leave the AP",
       WiFi.SSID().c_str());
  if (onJoinedFromAp_) onJoinedFromAp_();
#endif
}

bool NetworkService::isConnected() const { return !apMode_ && WiFi.status() == WL_CONNECTED; }

std::string NetworkService::ip() const {
  return std::string((apMode_ ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
}

}
