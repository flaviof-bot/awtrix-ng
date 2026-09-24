#include "persistence/LittleFsRestoreSink.h"

namespace awtrix::backup {
bool LittleFsRestoreSink::beginFile(const std::string& path, std::string& err) {
  abortFile();
  // RestoreApplier validates archive paths; also protect direct users of the sink.
  const auto slash = path.find_last_of('/');
  const auto dir = path.substr(0, slash);
  if (slash == std::string::npos || slash + 1 == path.size() ||
      path.find("..") != std::string::npos || path.find('\\') != std::string::npos ||
      path.find('\0') != std::string::npos ||
      (dir != "/ICONS" && dir != "/MELODIES" && dir != "/PALETTES" &&
       dir != "/SCRIPTS" && dir != "/MP3")) {
    err = "invalid asset path"; return false;
  }
  if (!LittleFS.exists(dir.c_str()) && !LittleFS.mkdir(dir.c_str())) {
    err = "could not create directory"; return false;
  }
  path_ = path; temp_ = path + ".restore.tmp";
  file_ = LittleFS.open(temp_.c_str(), "w");
  failed_ = !file_;
  if (failed_) err = "could not open for writing";
  return !failed_;
}
bool LittleFsRestoreSink::writeFile(const uint8_t* data, std::size_t n) {
  if (!file_ || failed_) return false;
  failed_ = file_.write(data, n) != n;
  return !failed_;
}
bool LittleFsRestoreSink::endFile() {
  if (!file_ || failed_) { abortFile(); return false; }
  file_.flush(); file_.close();
  const bool ok = LittleFS.rename(temp_.c_str(), path_.c_str());
  if (!ok) { abortFile(); return false; }
  path_.clear(); temp_.clear(); return true;
}
void LittleFsRestoreSink::abortFile() {
  if (file_) file_.close();
  if (!temp_.empty()) LittleFS.remove(temp_.c_str());
  path_.clear(); temp_.clear(); failed_ = false;
}
}
