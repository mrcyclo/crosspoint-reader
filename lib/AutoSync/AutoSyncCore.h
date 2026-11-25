#pragma once

#include <cstddef>
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

// Reopening the same book within this many minutes of it leaving the screen skips the sync: a short
// break leaves no time to read it elsewhere, and each sync costs a Wi-Fi join and a restart. 0 never skips.
constexpr uint8_t SKIP_MINUTES_CHOICES[] = {0, 5, 10, 15};
constexpr uint8_t DEFAULT_SKIP_MINUTES = 15;
constexpr size_t SKIP_MINUTES_COUNT = sizeof(SKIP_MINUTES_CHOICES) / sizeof(SKIP_MINUTES_CHOICES[0]);

constexpr bool isSkipMinutesChoice(const uint8_t minutes) {
  for (const uint8_t choice : SKIP_MINUTES_CHOICES) {
    if (choice == minutes) return true;
  }
  return false;
}

// The choice after `minutes`, wrapping to the first; an unknown value restarts at the first.
constexpr uint8_t nextSkipMinutes(const uint8_t minutes) {
  for (size_t i = 0; i < SKIP_MINUTES_COUNT; i++) {
    if (SKIP_MINUTES_CHOICES[i] == minutes) return SKIP_MINUTES_CHOICES[(i + 1) % SKIP_MINUTES_COUNT];
  }
  return SKIP_MINUTES_CHOICES[0];
}

// The skip window for a stored minute value; one that is not a choice uses the default.
constexpr uint32_t skipWindowSeconds(const uint8_t minutes) {
  return (isSkipMinutesChoice(minutes) ? minutes : DEFAULT_SKIP_MINUTES) * 60u;
}

// Epoch seconds; 0 means unknown, which never counts as recent.
bool readRecently(int64_t nowEpoch, int64_t stoppedEpoch, uint32_t windowSeconds);

struct OpenContext {
  bool syncOnOpenEnabled;
  bool hasCredentials;
  bool silentBoot;
  bool readRecently;
};
bool shouldSyncOnOpen(const OpenContext& ctx);

}  // namespace autosync
