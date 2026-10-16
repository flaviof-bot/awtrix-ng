#pragma once

#include <algorithm>

#include "core/render/Canvas.h"

namespace awtrix {
namespace render {

inline constexpr int kContentBandHeight = 8;

// Legacy text and icons occupy eight rows; odd spare rows go below the band.
inline int contentBandTop(int height) {
  return height > kContentBandHeight ? (height - kContentBandHeight) / 2 : 0;
}

// Allocation-free, clipped view into the panel. Coordinates remain those of an eight-row
// display, preserving font baselines and clipping for every font. The view must not outlive
// panel. Full-canvas content must use panel instead. With enabled false (a platform that
// enlarges pages itself, see RenderCtx::contentBand) the view is the whole panel.
inline Canvas contentBand(Canvas& panel, bool enabled = true) {
  if (panel.width() <= 0 || panel.height() <= 0) return Canvas(0, 0, nullptr);
  if (!enabled) return Canvas(panel.width(), panel.height(), panel.data());
  return Canvas(panel.width(), std::min(panel.height(), kContentBandHeight),
                panel.data() + contentBandTop(panel.height()) * panel.width());
}

}
}
