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

struct OpenContext {
  bool autoSyncEnabled;
  bool hasCredentials;
  bool silentBoot;
  bool isEpub;
};
bool shouldSyncOnOpen(const OpenContext& ctx);

}  // namespace autosync
