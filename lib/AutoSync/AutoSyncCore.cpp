#include "AutoSyncCore.h"

namespace autosync {

bool shouldSyncOnOpen(const OpenContext& ctx) {
  return ctx.autoSyncEnabled
      && ctx.hasCredentials
      && !ctx.silentBoot
      && ctx.isEpub;
}

}  // namespace autosync
