#include "AutoSyncCore.h"

namespace autosync {

bool readRecently(const int64_t nowEpoch, const int64_t stoppedEpoch) {
  if (nowEpoch <= 0 || stoppedEpoch <= 0 || nowEpoch < stoppedEpoch) return false;
  return nowEpoch - stoppedEpoch < RECENT_READ_SECONDS;
}

bool shouldSyncOnOpen(const OpenContext& ctx) {
  return ctx.syncOnOpenEnabled && ctx.hasCredentials && !ctx.silentBoot && !ctx.readRecently;
}

}  // namespace autosync
