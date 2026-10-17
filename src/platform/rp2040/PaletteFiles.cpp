#include "platform/rp2040/PaletteFiles.h"

#include <LittleFS.h>

#include <string>
#include <string_view>

#include "core/render/Palette.h"
#include "core/render/PaletteFile.h"
#include "core/render/PaletteStore.h"

namespace awtrix::pico {
// The ESP32 entry point's loader, through the same PaletteStore mechanism. arduino-pico's open()
// has no default mode, so the directory is opened with an explicit "r".
void installPaletteLoader() {
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    return render::loadPaletteFile(name, out, [](const std::string& leaf, std::string& text) {
      File f = LittleFS.open((std::string("/PALETTES/") + leaf).c_str(), "r");
      if (!f) return false;
      text.reserve(static_cast<std::size_t>(f.size()));
      while (f.available()) text.push_back(static_cast<char>(f.read()));
      return true;
    }, [](const render::PaletteNameVisitor& visit) {
      File dir = LittleFS.open("/PALETTES", "r");
      if (!dir) return;
      for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        std::string_view leaf = e.name() ? e.name() : "";
        const std::size_t slash = leaf.rfind('/');
        if (slash != std::string_view::npos) leaf.remove_prefix(slash + 1);
        if (visit(leaf)) break;
      }
    });
  });
}
}
