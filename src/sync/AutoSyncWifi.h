#pragma once

#include <AutoSyncCore.h>

namespace autosync {

bool connectSavedWifi(const Deadline& deadline);
void shutdownWifi();

}  // namespace autosync
