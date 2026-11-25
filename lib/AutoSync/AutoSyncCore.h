#pragma once

#include <cstdint>

namespace autosync {

constexpr uint32_t SESSION_BUDGET_MS = 15000;

class Deadline {
 public:
  Deadline(uint32_t startMs, uint32_t budgetMs) : startMs(startMs), budgetMs(budgetMs) {}

  bool expired(uint32_t nowMs) const { return nowMs - startMs >= budgetMs; }
  uint32_t remaining(uint32_t nowMs) const { return expired(nowMs) ? 0 : budgetMs - (nowMs - startMs); }

 private:
  uint32_t startMs;
  uint32_t budgetMs;
};

// Reopening the same book this soon after it was last on screen skips the sync: a short break
// leaves no time to read it elsewhere, and each sync costs a Wi-Fi join and a restart.
constexpr int64_t RECENT_READ_SECONDS = 15 * 60;

// Epoch seconds; 0 means unknown, which never counts as recent.
bool readRecently(int64_t nowEpoch, int64_t stoppedEpoch);

struct OpenContext {
  bool syncOnOpenEnabled;
  bool hasCredentials;
  bool silentBoot;
  bool readRecently;
};
bool shouldSyncOnOpen(const OpenContext& ctx);

}  // namespace autosync
