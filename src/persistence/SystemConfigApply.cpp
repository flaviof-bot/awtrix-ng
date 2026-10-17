#include "persistence/SystemConfigApply.h"

#include "core/ConfigRules.h"

namespace awtrix {
namespace sysconfig {

bool apply(DeviceConfig& cfg, api::JsonReader obj, int& applied, ApplyError& err, Origin origin) {
  applied = 0;
  cfgrules::ConfigError cerr;
#if defined(AWTRIX_PLATFORM_RP2040)
  // A cross-board backup must not replace the built-in panel or soldered pins.
  std::string restored;
  if (origin == Origin::Restore) {
    api::JsonWriter w(restored);
    w.beginObject();
    auto r = obj;
    if (r.enterObject()) while (r.nextMember()) {
      const std::string key(r.key());
      if (key.compare(0, 3, "pin") != 0 && key.compare(0, 5, "panel") != 0)
        w.key(key.c_str()).raw(r.valueText());
      if (!r.skipValue()) break;
    }
    w.endObject();
    obj = api::JsonReader(restored);
  }
#endif
  // A restore is allowed to write empty strings to clear a field; an interactive edit is not,
  // because a blank box in the UI means "leave it alone".
  const bool allowEmptyClears = origin == Origin::Restore;
  // Fields the platform does not offer are dropped unread, so a backup from a device that has
  // them restores everything else.
  if (!cfgrules::validateSystemRead(obj, cerr, allowEmptyClears, PlatformConfig::ignoredFields(),
                                  static_cast<const PlatformConfig*>(&cfg))) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  // Everything is merged into a copy and only assigned back once all the cross-field rules pass,
  // so a rejected request cannot leave the live config half applied.
  DeviceConfig merged = cfg;
  const std::string ssid = merged.wifiSsid, pass = merged.wifiPass;
  applied = merged.applyRead(obj);
  // A restored backup keeps the credentials this device is connected with — the backup may come
  // from another network, and taking its Wi-Fi settings would strand the device.
  if (origin == Origin::Restore) {
    merged.wifiSsid = ssid;
    merged.wifiPass = pass;
  }
  const cfgrules::IpSplit split = cfgrules::systemIpSplit(obj);
  if (split.present) {
    merged.ip = split.ip;
    merged.subnet = split.subnet;
    ++applied;
  }
  // Panel and pin fields a platform does not offer keep what is stored, which nobody can change,
  // so only the fields a request can reach are checked.
  if ((merged.panelConfigurable &&
       !cfgrules::validateMatrixGeometry(merged.panelWidth, merged.panels, cerr)) ||
      !cfgrules::validateBrightnessWindow(merged.minBrightness, merged.maxBrightness, cerr) ||
      !cfgrules::validateStaticNet(merged.netStatic, merged.ip, merged.subnet, merged.gateway, merged.dns1,
                                   merged.dns2, cerr) ||
      !cfgrules::validateMqttGate(merged.mqttEnabled, merged.mqttHost, cerr) ||
      !cfgrules::validateAuthGate(merged.authEnabled, merged.authUser, merged.authPass, cerr) ||
      !merged.validate(cerr) ||
      (merged.gpioConfigurable &&
       !cfgrules::validateAudioPins(merged.pinI2sBclk, merged.pinI2sLrclk, merged.pinI2sDout,
                                    merged.pinI2sMclk, merged.pinAmpEnable, cerr))) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
#if defined(AWTRIX_PLATFORM_RP2040)
  if (!cfgrules::validateFixedPanel(pins::activeProfile(), merged.panelWidth, merged.panels,
                                   merged.panelHeight, cerr)) {
    err = {422, "validationFailed", cerr.message, cerr.field};
    return false;
  }
  if (merged.panelStart != cfg.panelStart || merged.panelWiring != cfg.panelWiring ||
      merged.panelColorOrder != cfg.panelColorOrder || merged.panelSerpentine != cfg.panelSerpentine ||
      merged.panelChainReverse != cfg.panelChainReverse ||
      merged.panelChainSerpentine != cfg.panelChainSerpentine) {
    err = {422, "validationFailed", "panel wiring is fixed", "panelWiring"};
    return false;
  }
#endif
  std::string pinErr;
#if defined(AWTRIX_PLATFORM_RP2040)
  if (!merged.validatePins(pinErr)) {
    err = {422, "validationFailed", pinErr, "pinMatrix"};
    return false;
  }
#endif
  if (merged.gpioConfigurable && !merged.validatePins(pinErr)) {
    err = {400, "invalidPinConfig", pinErr, ""};
    return false;
  }
  cfg = merged;
  return true;
}

}
}
