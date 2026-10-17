#include "core/input/ButtonWebhook.h"

#include "core/api/JsonText.h"

namespace awtrix::input {

// The uid is escaped. A real uid (a MAC, or a checked Linux uid) is hex or alphanumeric and comes
// out unchanged; only a malformed one would otherwise break the JSON.
std::string webhookPressBody(const char* control, bool state, const std::string& uid) {
  std::string out = std::string("{\"button\":\"") + control + "\",\"state\":" +
                    (state ? "true" : "false") + ",\"uid\":";
  api::appendJsonString(out, uid);
  out += '}';
  return out;
}

std::string webhookTurnBody(int turn, const std::string& uid) {
  std::string out = std::string("{\"button\":\"") + kWebhookKnobName + "\",\"turn\":" +
                    std::to_string(turn) + ",\"uid\":";
  api::appendJsonString(out, uid);
  out += '}';
  return out;
}

}
