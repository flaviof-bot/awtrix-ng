#pragma once

#include "core/FeatureSet.h"
#include "core/render/DisplayProfile.h"

namespace awtrix {
namespace pins { struct SocProfile; }

// Filled in by the entry point.
struct PlatformDescriptor {
  const char* id = "";
  DisplayProfile display;
  bool configurableGpio = true;
  bool lightSensor = false;
  // A PCM input can feed visualization, independently of audio output capabilities.
  bool microphonePcm = false;
  // What the build compiled in; reported as capabilities and enforced by the API router.
  FeatureSet features{};
  // Soldered wiring a board reports in place of configurable GPIO (configurableGpio false).
  const pins::SocProfile* fixedGpio = nullptr;
};

}
