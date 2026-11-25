#include "AutoSyncCore.h"

namespace autosync {

bool readRecently(const int64_t nowEpoch, const int64_t stoppedEpoch, const uint32_t windowSeconds) {
  if (nowEpoch <= 0 || stoppedEpoch <= 0 || nowEpoch < stoppedEpoch) return false;
  return nowEpoch - stoppedEpoch < windowSeconds;
}

bool shouldSyncOnOpen(const OpenContext& ctx) {
  return ctx.syncOnOpenEnabled && ctx.hasCredentials && !ctx.silentBoot && !ctx.readRecently;
}

}  // namespace autosync
