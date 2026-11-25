#include "AutoSyncRecent.h"

#include <AutoSyncCore.h>
#include <Fnv1a.h>
#include <TrustedTime.h>
#include <esp_attr.h>

#include <cstdint>

namespace autosync {
namespace {

constexpr uint32_t LAST_READ_MAGIC = 0x52454144;

struct LastRead {
  uint32_t magic;
  uint32_t pathHash;
  int64_t stoppedAt;
};
RTC_NOINIT_ATTR LastRead lastRead;

uint32_t pathHash(const std::string& bookPath) { return fnv1a::hash(bookPath.data(), bookPath.size()); }

}  // namespace

void noteReadingStopped(const std::string& bookPath) {
  const int64_t now = trustedtime::trustedNow();
  if (now == 0 || bookPath.empty()) {
    lastRead.magic = 0;
    return;
  }
  lastRead.pathHash = pathHash(bookPath);
  lastRead.stoppedAt = now;
  lastRead.magic = LAST_READ_MAGIC;
}

bool bookReadRecently(const std::string& bookPath, const uint32_t windowSeconds) {
  if (lastRead.magic != LAST_READ_MAGIC || lastRead.pathHash != pathHash(bookPath)) return false;
  return readRecently(trustedtime::trustedNow(), lastRead.stoppedAt, windowSeconds);
}

}  // namespace autosync
