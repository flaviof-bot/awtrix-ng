#pragma once

namespace awtrix {
// What this build compiled in, separate from the audio sinks actually installed at runtime.
// Every field is on unless a build turns it off (platform/BuildFeatures.h).
struct FeatureSet {
  bool scripting = true;
  bool mp3 = true;
  bool radio = true;
  bool outboundTls = true;
  bool browserOta = true;

  constexpr bool all() const { return scripting && mp3 && radio && outboundTls && browserOta; }
};
}
