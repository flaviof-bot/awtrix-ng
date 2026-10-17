#if defined(AWTRIX_PLATFORM_RP2040)
// A connection whose request head is read off the socket ahead of WebServer's parser. The parser
// then reads that head back from here, and the rest of the request from the socket. lwIP keeps
// each TCP segment in its own buffer and WiFiClient::peekBytes() sees only the first, so a head
// that spans segments could not be checked in place.
class HeadClient : public WiFiClient {
 public:
  enum class Head { Waiting, Ready, TooLong };

  HeadClient(const WiFiClient& accepted, char* buffer, size_t capacity)
      : WiFiClient(accepted), buf_(buffer), cap_(capacity) {}

  // Moves what has arrived into the buffer. Ready once the head is in, up to its blank line, and
  // a body small enough to be read in one piece has arrived as well; TooLong if the buffer fills
  // before the head ends.
  Head gather() {
    if (!headEnd_) {
      const int more = WiFiClient::available();
      if (more > 0 && len_ < cap_)
        len_ += std::max(0, WiFiClient::read(reinterpret_cast<uint8_t*>(buf_ + len_),
                                             std::min(cap_ - len_, static_cast<size_t>(more))));
      headEnd_ = requesthead::end(buf_, len_, scanned_);
      scanned_ = len_;
      if (!headEnd_) return len_ == cap_ ? Head::TooLong : Head::Waiting;
    }
    const long body = requesthead::contentLength(buf_, headEnd_);
    if (body <= 0 || body > kArenaBodyThresholdBytes) return Head::Ready;
    const size_t have = len_ - headEnd_ + static_cast<size_t>(std::max(0, WiFiClient::available()));
    return have >= static_cast<size_t>(body) ? Head::Ready : Head::Waiting;
  }

  int available() override { return static_cast<int>(pending()) + WiFiClient::available(); }
  int read() override {
    return pending() ? static_cast<uint8_t>(buf_[pos_++]) : WiFiClient::read();
  }
  int read(uint8_t* dst, size_t size) override {
    const size_t n = std::min(size, pending());
    if (!n) return WiFiClient::read(dst, size);
    memcpy(dst, buf_ + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }
  int peek() override { return pending() ? static_cast<uint8_t>(buf_[pos_]) : WiFiClient::peek(); }
  size_t peekBytes(uint8_t* dst, size_t size) override {
    const size_t n = std::min(size, pending());
    if (!n) return WiFiClient::peekBytes(dst, size);
    memcpy(dst, buf_ + pos_, n);
    return n;
  }

 private:
  size_t pending() const { return len_ - pos_; }

  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  size_t pos_ = 0;
  size_t scanned_ = 0;
  size_t headEnd_ = 0;
};
#endif

class RawWebServer : public WebServer {
 public:
  using WebServer::WebServer;
#if defined(AWTRIX_PLATFORM_RP2040)
  void setRawReadTimeout(unsigned long ms) {
    if (_currentClient) _currentClient->Stream::setTimeout(ms);
  }
#else
  void setRawReadTimeout(unsigned long ms) { _currentClient.Stream::setTimeout(ms); }
#endif

#if defined(AWTRIX_PLATFORM_RP2040)
  // WebServer parses the request line and headers, and reads a small body, with blocking reads
  // that wait up to 5 s per byte, so a client trickling them could hold loop() past the 8 s
  // watchdog. The parser runs only once HeadClient holds the whole head, up to its blank line,
  // and, for a body small enough to be read in one piece, the whole body has arrived too. A head
  // longer than kHeadBytes is answered 431 and closed; a client that has not got that far within
  // HTTP_MAX_DATA_WAIT is dropped, as a silent one is. Larger bodies arrive through the raw and
  // upload callbacks, which feed the watchdog. WebServer would parse a request in the same pass
  // that accepts it, before this check could run, so a connection is accepted here and handed to
  // WebServer on a later pass. WebServer only ever sees HeadClients: it creates a client itself
  // only on a pass that starts without one, and this override never lets it do that.
  void handleClient() override {
    if (_currentStatus == HC_NONE) {
      WiFiClient accepted = getServer().accept();
      if (!accepted) return;
      delete _currentClient;
      _currentClient = new HeadClient(accepted, head_, sizeof(head_));
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
      return;
    }
    if (_currentStatus == HC_WAIT_READ && _currentClient) {
      switch (static_cast<HeadClient*>(_currentClient)->gather()) {
        case HeadClient::Head::Ready:
          break;
        case HeadClient::Head::TooLong:
          refuse(kHeadTooLong, sizeof(kHeadTooLong) - 1);
          return;
        case HeadClient::Head::Waiting:
          // WebServer applies the same timeout to a client that has sent nothing yet.
          if (!_currentClient->available()) break;
          if (millis() - _statusChange > HTTP_MAX_DATA_WAIT) refuse(nullptr, 0);
          return;
      }
    }
    WebServer::handleClient();
  }

 protected:
  // Every response byte, headers included, goes through these two. A plain write() keeps going
  // for as long as a slow reader drains a little every few seconds, which can outlast the
  // watchdog; write only what the send buffer has room for and feed the watchdog while waiting.
  // A client that takes nothing for HTTP_MAX_SEND_WAIT is given up on and its connection closed.
  size_t _currentClientWrite(const char* b, size_t l) override { return writeFed(b, l); }
  size_t _currentClientWrite_P(PGM_P b, size_t l) override { return writeFed(b, l); }

 private:
  static constexpr size_t kHeadBytes = 2048;
  static constexpr size_t kWriteSliceBytes = 1460; // one TCP segment
  static constexpr char kHeadTooLong[] =
      "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n"
      "Content-Length: 0\r\n\r\n";

  // One request is handled at a time, so the connection being read owns this buffer.
  char head_[kHeadBytes];

  // Answers with reply, if any, and closes the connection. What the client has already sent is
  // read and dropped first: closing with it unread would reset the connection and lose the reply.
  // Released here as well, since its unread bytes would bring it straight back to gather().
  void refuse(const char* reply, size_t length) {
    uint8_t sink[256];
    for (int left = _currentClient->available(); left > 0;) {
      const int n = _currentClient->read(sink, std::min(sizeof(sink), static_cast<size_t>(left)));
      if (n <= 0) break;
      left -= n;
    }
    if (reply) writeFed(reply, length);
    _currentClient->stop();
    delete _currentClient;
    _currentClient = nullptr;
    _currentStatus = HC_NONE;
  }

  size_t writeFed(const char* b, size_t l) {
    size_t done = 0;
    unsigned long progress = millis();
    while (done < l && _currentClient->connected()) {
      watchdog::feed();
      const int room = _currentClient->availableForWrite();
      if (room > 0) {
        const size_t n = _currentClient->write(
            b + done, std::min({l - done, static_cast<size_t>(room), kWriteSliceBytes}));
        if (n) {
          done += n;
          progress = millis();
          continue;
        }
      }
      if (millis() - progress > HTTP_MAX_SEND_WAIT) {
        // Closed, not just left: a response goes out in many writes (a file 1 KB at a time), and
        // each would otherwise wait out its own HTTP_MAX_SEND_WAIT. Once closed, they return at once.
        _currentClient->stop();
        break;
      }
      delay(1);
    }
    return done;
  }

#endif
 public:
  // WebServer::send() for a std::string body.
  void sendBody(int status, const char* contentType, const std::string& body) {
    String header;
    _prepareHeader(header, status, contentType, body.size());
    _currentClientWrite(header.c_str(), header.length());
    if (!body.empty()) sendContent(body.data(), body.size());
  }

#if !defined(AWTRIX_PLATFORM_RP2040)
  // WebServer serves one client at a time. Browsers like to open a socket and send nothing, which
  // would stall every other request, so drop a silent client as soon as someone else is waiting.
  void handleClient() override {
    if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
        millis() - _statusChange > kSilentClientGraceMs && _server.hasClient()) {
      _currentClient.stop();
    }
    WebServer::handleClient();
  }
#endif
};

#if defined(AWTRIX_PLATFORM_RP2040)
// arduino-pico's lwIP has five TCP pcbs, and ClientContext drops every accepted connection to
// TCP_PRIO_MIN while the listener stays at TCP_PRIO_NORMAL. A SYN that finds the pool full then
// makes lwIP abort the oldest accepted connection to make room, so a browser opening eight sockets
// at once saw two or three of them reset mid-request. At its clients' priority the listener can no
// longer evict them: the extra SYN is dropped, and the client's retransmit gets in once a request
// has been answered.
struct ListenPcb : WiFiServer {
  static tcp_pcb* of(WiFiServer& server) { return server.*(&ListenPcb::_listen_pcb); }
};

// lwIP reports that dropped SYN as accept(NULL, ERR_MEM), and WiFiServer would wrap the NULL pcb in
// a client and fault. Only real connections reach it.
tcp_accept_fn serverAccept = nullptr;
err_t acceptConnected(void* arg, tcp_pcb* pcb, err_t err) {
  if (pcb == nullptr || err != ERR_OK) return ERR_MEM;
  return serverAccept(arg, pcb, err);
}

// The free heap a request body may be copied into. newlib cannot report the largest free block,
// so the total stands in for it.
std::size_t bodyCopyRoom() { return rp2040.getFreeHeap(); }

// The last path element: arduino-pico's File::name() is the leaf on some versions, the path on
// others.
std::string leafName(const char* name) {
  const std::string path = name ? name : "";
  return path.substr(path.find_last_of('/') + 1);
}

bool isFile(const std::string& path) {
  File file = LittleFS.open(path.c_str(), "r");
  return file && !file.isDirectory();
}
#endif

const char* methodName(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "ANY";
  }
}

#if defined(AWTRIX_PLATFORM_RP2040)
// arduino-pico has no VFS, so directories are walked with the LittleFS File API.
bool folderEmpty(const std::string& dir) {
  File folder = LittleFS.open(dir.c_str(), "r");
  if (!folder || !folder.isDirectory()) return false;
  File entry = folder.openNextFile();
  return !entry;
}

// SHA-256 of a file in 256-byte reads through BearSSL, which arduino-pico ships; size is what was
// read.
__attribute__((noinline)) bool sha256File(const std::string& path, std::string& hex, uint32_t& size) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file || file.isDirectory()) return false;
  br_sha256_context sha;
  br_sha256_init(&sha);
  uint8_t buf[256];
  size = 0;
  int n;
  while ((n = file.read(buf, sizeof(buf))) > 0) {
    br_sha256_update(&sha, buf, static_cast<size_t>(n));
    size += static_cast<uint32_t>(n);
  }
  file.close();
  if (n < 0) return false;
  uint8_t digest[br_sha256_SIZE];
  br_sha256_out(&sha, digest);
  static const char kHex[] = "0123456789abcdef";
  hex.clear();
  hex.reserve(2 * sizeof(digest));
  for (const uint8_t b : digest) {
    hex += kHex[b >> 4];
    hex += kHex[b & 0x0f];
  }
  return true;
}
#else
bool folderEmpty(const std::string& dir) {
  DIR* folder = ::opendir(fs::vfsPath(dir).c_str());
  if (!folder) return false;
  bool empty = true;
  while (const dirent* entry = ::readdir(folder)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    empty = false;
    break;
  }
  ::closedir(folder);
  return empty;
}

// SHA-256 of a file in 256-byte reads; size is what was read.
__attribute__((noinline)) bool sha256File(const std::string& path, std::string& hex, uint32_t& size) {
  const int fd = fs::openRead(path);
  if (fd < 0) return false;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  uint8_t buf[256];
  size = 0;
  while (ok) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) {
      ok = n == 0;
      break;
    }
    ok = mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n)) == 0;
    size += static_cast<uint32_t>(n);
  }
  uint8_t digest[32];
  ok = ok && mbedtls_sha256_finish_ret(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  ::close(fd);
  if (!ok) return false;
  static const char kHex[] = "0123456789abcdef";
  hex.clear();
  hex.reserve(2 * sizeof(digest));
  for (const uint8_t b : digest) {
    hex += kHex[b >> 4];
    hex += kHex[b & 0x0f];
  }
  return true;
}
#endif


constexpr const char* kAssetUploadTemporary = "/.http-asset.tmp";

bool makeDirectory(const std::string& directory, bool& created) {
  created = false;
  if (LittleFS.exists(directory.c_str())) return true;
  created = LittleFS.mkdir(directory.c_str());
  return created;
}

void pruneDirectory(const std::string& directory) {
  if (folderEmpty(directory)) LittleFS.rmdir(directory.c_str());
}

// streamFile() writes straight to the client, past the Pico's fed writes, so there a file goes out
// through sendContent() in pieces instead. Other boards keep streamFile().
void streamFileFed(WebServer& server, File& f, const char* type) {
#if defined(AWTRIX_PLATFORM_RP2040)
  server.setContentLength(f.size());
  server.send(200, type, "");
  char buf[1024];
  while (server.client().connected()) {
    const size_t n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
    if (n == 0) break;
    server.sendContent(buf, n);
  }
#else
  server.streamFile(f, type);
#endif
}

class EspFiles final : public api::Files {
 public:
  EspFiles(const api::scriptsounds::InstalledScripts& installed,
           const std::function<void(const std::string&)>& release,
           const std::function<void()>& changed)
      : Files(installed, iconorigins::storage()), release_(release), changed_(changed) {}
  void list(const std::string& directory, const Visit& visit) override {
    if (directory.empty()) return;
#if defined(AWTRIX_PLATFORM_RP2040)
    File folder = LittleFS.open(directory.c_str(), "r");
    if (!folder || !folder.isDirectory()) return;
    for (File entry = folder.openNextFile(); entry; entry = folder.openNextFile()) {
      const bool isDirectory = entry.isDirectory();
      const std::string name = leafName(entry.name());
      visit({name.c_str(), isDirectory ? 0u : static_cast<uint64_t>(entry.size()), isDirectory});
    }
#else
    if (DIR* folder = ::opendir(fs::vfsPath(directory).c_str())) {
      while (const dirent* entry = ::readdir(folder)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        const bool isDirectory = entry->d_type == DT_DIR;
        const long size = isDirectory ? 0 : fs::fileSize(directory + "/" + entry->d_name);
        if (size >= 0) visit({entry->d_name, static_cast<uint64_t>(size), isDirectory});
      }
      ::closedir(folder);
    }
#endif
  }
#if defined(AWTRIX_PLATFORM_RP2040)
  bool exists(const std::string& path) override { return isFile(path); }
#else
  bool exists(const std::string& path) override { return fs::isFile(path); }
#endif
  bool read(const std::string& path, std::string& content) override {
    media::PodBuffer<uint8_t> bytes;
    if (!media::readAsset(path, bytes)) return false;
    content.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
  }
  bool write(const std::string& path, const std::string& bytes) override {
    return document::write(path.c_str(), bytes);
  }
  bool remove(const std::string& path) override { return LittleFS.remove(path.c_str()); }
  bool rename(const std::string& from, const std::string& to) override {
    return LittleFS.rename(from.c_str(), to.c_str());
  }
  bool mkdir(const std::string& directory, bool& created) override { return makeDirectory(directory, created); }
  void prune(const std::string& directory) override { pruneDirectory(directory); }
  void usage(uint64_t& used, uint64_t& total) override {
    std::size_t totalBytes = 0, usedBytes = 0;
    fs::usage(totalBytes, usedBytes);
    used = usedBytes; total = totalBytes;
  }
  bool etag(const std::string& path, std::string& value) override {
#if defined(AWTRIX_PLATFORM_RP2040)
    File file = LittleFS.open(path.c_str(), "r");
    if (!file || file.isDirectory()) return false;
    api::Etag tag;
    uint8_t buffer[256];
    int size;
    while ((size = file.read(buffer, sizeof(buffer))) > 0) tag.append(buffer, size);
    file.close();
#else
    const int fd = fs::openRead(path);
    if (fd < 0) return false;
    api::Etag tag;
    uint8_t buffer[256];
    ssize_t size;
    while ((size = ::read(fd, buffer, sizeof(buffer))) > 0) tag.append(buffer, size);
    ::close(fd);
#endif
    if (size < 0) return false;
    value = tag.value();
    return true;
  }
  bool sha256(const std::string& path, std::string& digest, uint64_t& size) override {
    uint32_t bytes = 0;
    const bool ok = sha256File(path, digest, bytes);
    size = bytes;
    if (!ok) logf("http: could not read %s", path.c_str());
    return ok;
  }
  void release(const std::string& path) override { if (release_) release_(path); }
  void changed() override { if (changed_) changed_(); }
 private:
  const std::function<void(const std::string&)>& release_;
  const std::function<void()>& changed_;
};

class EspReply final : public api::Reply {
 public:
  explicit EspReply(WebServer& server) : server_(server) {}
  void send(const api::HttpResult& result, bool streaming = false) override {
    if (streaming) {
      server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server_.send(result.status, result.contentType, "");
    } else static_cast<RawWebServer&>(server_).sendBody(result.status, result.contentType, result.body);
  }
  void header(const char* name, const std::string& value) override { server_.sendHeader(name, value.c_str()); }
  void chunk(const char* data, std::size_t size) override {
    // Batch listing entries: an individual sendContent costs about 9 ms on the device.
    if (batch_.capacity() < kListBatchBytes) batch_.reserve(kListBatchBytes + kListEntryReserveBytes);
    batch_.append(data, size);
    if (size == 0 || batch_.size() >= kListBatchBytes) {
      if (!batch_.empty()) server_.sendContent(batch_.data(), batch_.size());
      batch_.clear();
    }
    if (size == 0) server_.sendContent("");
  }
  bool sendFile(const std::string& path, const char* contentType) override {
    File file = LittleFS.open(path.c_str(), "r");
    if (!file || file.isDirectory()) { if (file) file.close(); return false; }
    streamFileFed(server_, file, contentType);
    file.close();
    return true;
  }
 private:
  WebServer& server_;
  std::string batch_;
};
