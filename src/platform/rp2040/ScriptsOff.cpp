#include "core/script/ScriptHost.h"

// Scripting is built out (AWTRIX_FEATURE_SCRIPTING=0): no ScriptHost is ever made and the runtime
// is always handed nullptr, but the link still needs the one member ApplicationRuntime names.
// M6a: removed when core/script/ScriptHost.cpp links on the Pico.
namespace awtrix::script {
void ScriptHost::tick(const RenderCtx&, const std::string&, const std::string&) {}
}
