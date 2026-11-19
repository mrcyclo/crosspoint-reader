#pragma once

#include <cstdint>
#include <optional>
#include <string>

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

// Last successfully synced position of one book.
struct Marker {
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  float percentage = 0.0f;
  bool pulledThisSession = false;
};

// Layout: version u8, spine u16, page u16, percentage f32, flags u8 (bit0 = pulledThisSession), little-endian.
std::string encodeMarker(const Marker& m);
std::optional<Marker> decodeMarker(const std::string& bytes);

// Inside the book's cache dir.
constexpr const char* MARKER_FILE = "autosync.bin";

bool samePosition(const Marker& m, uint16_t spineIndex, uint16_t pageNumber);

enum class SleepPush { Skip, PushDirect, CompareThenPush };
SleepPush decideSleepPush(const std::optional<Marker>& marker, uint16_t spineIndex, uint16_t pageNumber);

struct OpenContext {
  bool autoSyncEnabled;
  bool hasCredentials;
  bool silentBoot;
  bool isEpub;
};
bool shouldSyncOnOpen(const OpenContext& ctx);
bool shouldSyncOnSleep(bool autoSyncEnabled, bool hasCredentials, bool inEpubReader);

}  // namespace autosync
