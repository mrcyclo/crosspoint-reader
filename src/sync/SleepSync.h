#pragma once

#include <string>

#include "CrossPointPosition.h"
#include "ProgressMapper.h"

// Push-only KOSync session on the way into sleep, bounded by autosync::SESSION_BUDGET_MS.
// Runs under the caller's RenderLock. The caller shows the status popup first and repaints its page afterwards.
void syncBeforeSleep(const std::string& epubPath, const CrossPointPosition& pos, const SavedProgressPosition& koPos);
