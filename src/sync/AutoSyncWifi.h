#pragma once

#include <AutoSyncCore.h>

namespace autosync {

// Reads and writes the Wi-Fi store on SD: hold a RenderLock around the call.
bool connectSavedWifi(const Deadline& deadline);
void shutdownWifi();

}  // namespace autosync
