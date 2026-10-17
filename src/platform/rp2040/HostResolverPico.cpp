#include "transport/net/HostResolver.h"

#include <WiFi.h>
#include "platform/rp2040/BoundedHostLookup.h"
#include "system/Watchdog.h"

namespace awtrix::net {
namespace {
// Answers Ready or Failed on the first call: each lookup is bounded instead of asynchronous.
class HostResolverPico final : public IHostResolver {
 public:
  ResolveState resolve(const std::string& host) override {
    watchdog::feed();
    const bool ready = lookup_.resolve(WiFi, host);
    watchdog::feed();
    return ready ? ResolveState::Ready : ResolveState::Failed;
  }
  // Host byte order, as the interface requires; IPAddress keeps the octets in network order.
  uint32_t address() const override {
    const IPAddress a = lookup_.address();
    return (uint32_t(a[0]) << 24) | (uint32_t(a[1]) << 16) | (uint32_t(a[2]) << 8) | a[3];
  }
  LinkError error() const override { return lookup_.error(); }
  void forget() override { lookup_.forget(); }
 private:
  platform::pico::BoundedHostLookup<IPAddress> lookup_;
};
}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolverPico());
}
}
