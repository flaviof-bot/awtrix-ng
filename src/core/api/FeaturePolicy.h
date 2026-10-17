#pragma once

#include <string>

#include "core/Command.h"
#include "core/FeatureSet.h"
#include "core/sound/SoundMp3.h"
#include "core/sound/SoundSpec.h"

namespace awtrix {
namespace api {

// First phase, before any body is read: a route that exists only for a feature this build left
// out. Paths are HTTP paths or MQTT topic suffixes.
inline DispatchResult featurePolicy(const FeatureSet& features, const std::string& path) {
  if (features.all()) return DispatchResult::Ok;
  struct Rule { const char* path; bool prefix; bool FeatureSet::*enabled; };
  static const Rule rules[] = {
      {"/api/v1/scripts/", true, &FeatureSet::scripting},
      {"/api/v1/apps/script/", true, &FeatureSet::scripting},
      {"/api/v1/apps/script-update/", true, &FeatureSet::scripting},
      {"/api/v1/audio/mp3", false, &FeatureSet::mp3},
      {"/api/v1/audio/mp3/", true, &FeatureSet::mp3},
      {"/api/v1/audio/stations", false, &FeatureSet::radio},
      {"cmd/audio/stations", false, &FeatureSet::radio},
      {"/update", false, &FeatureSet::browserOta},
  };
  for (const auto& rule : rules)
    if (!(features.*rule.enabled) &&
        (rule.prefix ? path.rfind(rule.path, 0) == 0 : path == rule.path))
      return DispatchResult::Unavailable;
  // A script's settings and data; the built-in apps' settings (/api/v1/apps/builtin/) stay.
  const auto endsWith = [&](const char* tail, std::size_t n) {
    return path.size() >= n && path.compare(path.size() - n, n, tail) == 0;
  };
  if (!features.scripting && path.rfind("/api/v1/apps/", 0) == 0 &&
      path.rfind("/api/v1/apps/builtin/", 0) != 0 &&
      (endsWith("/config", 7) || endsWith("/data", 5)))
    return DispatchResult::Unavailable;
  return DispatchResult::Ok;
}

namespace detail {
inline bool needsTls(const std::string& url) { return url.rfind("https://", 0) == 0; }

inline bool playable(const FeatureSet& features, const sound::Spec& spec) {
  switch (spec.kind) {
    case sound::Kind::Station:
      return features.radio && (features.outboundTls || !needsTls(spec.text));
    case sound::Kind::File:
      return features.mp3 && (features.outboundTls || !needsTls(spec.text));
    default:
      return true;
  }
}
}

// Second phase, after the bounded parse and before anything runs: a shared route whose payload
// picks the feature. A sound list stays playable while one of its entries is.
inline DispatchResult featurePolicy(const FeatureSet& features, const Command& cmd) {
  if (features.all()) return DispatchResult::Ok;
  switch (cmd.type) {
    case CommandType::ScriptSet:
    case CommandType::ScriptUpdate:
    case CommandType::ScriptConfigSet:
    case CommandType::ScriptDataSet:
    case CommandType::ScriptRemove:
      return features.scripting ? DispatchResult::Ok : DispatchResult::Unavailable;
    case CommandType::SetRadioStations:
      return features.radio ? DispatchResult::Ok : DispatchResult::Unavailable;
    case CommandType::PlayAudio: {
      sound::Choices choices;
      DispatchDetail detail;
      // A malformed sound is the dispatcher's to report.
      if (!sound::parse(cmd.payload, sound::Origin::Play, choices, detail)) return DispatchResult::Ok;
      for (const auto& spec : choices)
        if (detail::playable(features, spec)) return DispatchResult::Ok;
      return choices.count ? DispatchResult::Unavailable : DispatchResult::Ok;
    }
    default:
      return DispatchResult::Ok;
  }
}

}
}
