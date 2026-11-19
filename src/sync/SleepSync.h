#pragma once

#include <string>

#include "CrossPointPosition.h"
#include "ProgressMapper.h"

// Push-only KOSync session on the way into sleep, bounded by autosync::SESSION_BUDGET_MS.
// Runs under the caller's RenderLock. The caller shows the status popup first and repaints its page afterwards.
void syncBeforeSleep(const std::string& epubPath, const CrossPointPosition& pos, const SavedProgressPosition& koPos);

// Set by enterDeepSleep() before activities prepare for sleep: false when Bluetooth did not stop or the battery
// is below the floor for bringing Wi-Fi up at sleep.
void setSleepSyncNetworkAllowed(bool allowed);
bool sleepSyncNetworkAllowed();
