#include "AutoSyncCore.h"

#include <cstring>

namespace autosync {

namespace {
constexpr uint8_t MARKER_VERSION = 1;
constexpr size_t MARKER_SIZE = 10;
constexpr uint8_t FLAG_PULLED = 0x01;
}  // namespace

std::string encodeMarker(const Marker& m) {
  uint32_t pctBits;
  static_assert(sizeof(pctBits) == sizeof(m.percentage), "float must be 32 bits");
  std::memcpy(&pctBits, &m.percentage, sizeof(pctBits));

  std::string out(MARKER_SIZE, '\0');
  out[0] = static_cast<char>(MARKER_VERSION);
  out[1] = static_cast<char>(m.spineIndex & 0xFF);
  out[2] = static_cast<char>(m.spineIndex >> 8);
  out[3] = static_cast<char>(m.pageNumber & 0xFF);
  out[4] = static_cast<char>(m.pageNumber >> 8);
  for (int i = 0; i < 4; i++) {
    out[5 + i] = static_cast<char>((pctBits >> (8 * i)) & 0xFF);
  }
  out[9] = static_cast<char>(m.pulledThisSession ? FLAG_PULLED : 0);
  return out;
}

std::optional<Marker> decodeMarker(const std::string& bytes) {
  if (bytes.size() != MARKER_SIZE) return std::nullopt;
  if (static_cast<uint8_t>(bytes[0]) != MARKER_VERSION) return std::nullopt;

  const auto b = [&bytes](size_t i) { return static_cast<uint32_t>(static_cast<uint8_t>(bytes[i])); };
  Marker m;
  m.spineIndex = static_cast<uint16_t>(b(1) | (b(2) << 8));
  m.pageNumber = static_cast<uint16_t>(b(3) | (b(4) << 8));
  const uint32_t pctBits = b(5) | (b(6) << 8) | (b(7) << 16) | (b(8) << 24);
  std::memcpy(&m.percentage, &pctBits, sizeof(m.percentage));
  m.pulledThisSession = (b(9) & FLAG_PULLED) != 0;
  return m;
}

bool samePosition(const Marker& m, uint16_t spineIndex, uint16_t pageNumber) {
  return m.spineIndex == spineIndex && m.pageNumber == pageNumber;
}

SleepPush decideSleepPush(const std::optional<Marker>& marker, uint16_t spineIndex, uint16_t pageNumber) {
  if (!marker) return SleepPush::CompareThenPush;
  if (samePosition(*marker, spineIndex, pageNumber)) return SleepPush::Skip;
  if (marker->pulledThisSession) return SleepPush::PushDirect;
  return SleepPush::CompareThenPush;
}

bool shouldSyncOnOpen(const OpenContext& ctx) {
  return ctx.autoSyncEnabled && ctx.hasCredentials && !ctx.silentBoot && ctx.isEpub;
}

bool shouldSyncOnSleep(bool autoSyncEnabled, bool hasCredentials, bool inEpubReader) {
  return autoSyncEnabled && hasCredentials && inEpubReader;
}

}  // namespace autosync
