#include "core/api/ApiRouter.h"
#include "core/script/ScriptConfig.h"
#include "core/script/ScriptData.h"
#include "core/script/ScriptHeap.h"
#include "core/script/ScriptHost.h"

// Scripting is built out (AWTRIX_FEATURE_SCRIPTING=0): no ScriptHost is ever made and the runtime
// and the HTTP state are always handed nullptr, but the link still needs the members they name.
// The script routes answer 503 in api::featurePolicy before any of these could run.
// M6a: removed when core/script/ScriptHost.cpp links on the Pico.
namespace awtrix::script {
void ScriptHost::tick(const RenderCtx&, const std::string&, const std::string&) {}
std::map<std::string, ScriptHost::Info> ScriptHost::list() const { return {}; }
std::vector<SharedEntry> ScriptHost::sharedSnapshot() const { return {}; }
int configResponse(const std::string&, const ConfigTextFn&, const ConfigTextFn&,
                   std::string& body) {
  body = api::errorJson("unavailable", "no scripting");
  return 503;
}
int dataResponse(const std::string&, const ConfigTextFn&, const ConfigTextFn&, std::string& body) {
  body = api::errorJson("unavailable", "no scripting");
  return 503;
}
namespace heap {
// The device state reports the pool as unavailable with no budget, and no body may grow.
Info info() { return {"unavailable", 0, false}; }
std::size_t growthBudget() { return 0; }
}
}
