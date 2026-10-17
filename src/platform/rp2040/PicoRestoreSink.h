#pragma once

#include <LittleFS.h>

#include <string>

#include "persistence/FsRestoreSink.h"

namespace awtrix::backup {
// 1.2.2's FsRestoreSink plus asset files through the LittleFS File API (arduino-pico has no
// VFS). Each asset is staged beside its target and renamed only once complete, so an aborted
// restore leaves no partial file, and every folder a nested entry needs is created.
class PicoRestoreSink : public FsRestoreSink {
 public:
  using FsRestoreSink::FsRestoreSink;
  bool beginFile(const std::string& path, std::string& err) override;
  bool writeFile(const uint8_t* data, std::size_t n) override;
  bool endFile() override;
  void abortFile() override;

 private:
  File file_;
  std::string path_, temp_;
  bool failed_ = false;
};
}
