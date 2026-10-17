#include <LittleFS.h>
#include <string>
#include "media/AssetFile.h"

namespace awtrix::media {
bool readAssetRange(std::string_view path, std::size_t offset, uint8_t* out,
                    std::size_t capacity, std::size_t& read, std::size_t& fileSize) {
  read = fileSize = 0;
  auto file = LittleFS.open(std::string(path).c_str(), "r");
  if (!file || file.isDirectory()) return false;
  fileSize = file.size();
  if (!fileSize || offset > fileSize || !file.seek(offset)) return false;
  const auto count = std::min(capacity, fileSize - offset);
  read = file.read(out, count);
  return read == count;
}
bool readAsset(std::string_view path, PodBuffer<uint8_t>& out, bool* outOfMemory) {
  if (outOfMemory) *outOfMemory = false;
  out.clear();
  auto file = LittleFS.open(std::string(path).c_str(), "r");
  if (!file || file.isDirectory() || !file.size()) return false;
  const auto size = file.size();
  if (!out.resize(size)) {
    if (outOfMemory) *outOfMemory = true;
    return false;
  }
  if (file.read(out.data(), size) != size) { out.clear(); return false; }
  return true;
}
}
