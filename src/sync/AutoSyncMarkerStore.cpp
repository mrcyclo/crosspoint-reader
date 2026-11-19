#include "AutoSyncMarkerStore.h"

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

namespace autosync {
namespace {

constexpr size_t MARKER_READ_CAP = 32;

std::string markerPath(const Epub& epub) { return epub.getCachePath() + "/" + MARKER_FILE; }

}  // namespace

std::optional<Marker> loadMarker(const std::string& epubPath) {
  const Epub epub(epubPath, "/.crosspoint");
  const std::string path = markerPath(epub);
  if (!Storage.exists(path.c_str())) return std::nullopt;

  std::string bytes;
  if (!Storage.readFileToString("AUTOSYNC", path, MARKER_READ_CAP, bytes)) return std::nullopt;
  return decodeMarker(bytes);
}

void saveMarker(const std::string& epubPath, const Marker& marker) {
  const Epub epub(epubPath, "/.crosspoint");
  epub.setupCacheDir();

  const std::string bytes = encodeMarker(marker);
  HalFile f;
  if (!Storage.openFileForWrite("AUTOSYNC", markerPath(epub), f)) {
    LOG_ERR("AUTOSYNC", "Could not open marker for write: %s", epubPath.c_str());
    return;
  }
  if (f.write(bytes.data(), bytes.size()) != bytes.size()) {
    LOG_ERR("AUTOSYNC", "Short marker write: %s", epubPath.c_str());
    return;
  }
  f.flush();
  LOG_DBG("AUTOSYNC", "Marker saved: spine=%u page=%u pct=%.4f pulled=%d", marker.spineIndex, marker.pageNumber,
          marker.percentage, marker.pulledThisSession ? 1 : 0);
}

void clearPulledThisSession(const std::string& epubPath) {
  auto marker = loadMarker(epubPath);
  if (!marker || !marker->pulledThisSession) return;

  marker->pulledThisSession = false;
  saveMarker(epubPath, *marker);
}

}  // namespace autosync
