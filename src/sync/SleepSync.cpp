#include "SleepSync.h"

#include <Arduino.h>
#include <AutoSyncCore.h>
#include <Epub.h>
#include <Logging.h>

#include <memory>
#include <optional>

#include "AutoSyncMarkerStore.h"
#include "AutoSyncWifi.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "KOReaderUpload.h"
#include "network/WifiPowerSaveGuard.h"

namespace {

using autosync::Deadline;
using autosync::Marker;
using autosync::SleepPush;

constexpr uint32_t MIN_REQUEST_BUDGET_MS = 500;
// Left for the popup and position mapping before this session and the page repaint and sleep screen after it.
constexpr uint32_t SLEEP_PATH_RESERVE_MS = 1500;

bool armRequest(const Deadline& deadline, const char* stage) {
  const uint32_t remaining = deadline.remaining(millis());
  if (remaining < MIN_REQUEST_BUDGET_MS) {
    LOG_INF("AUTOSYNC", "Sleep sync time budget spent before %s (%u ms left)", stage, static_cast<unsigned>(remaining));
    return false;
  }
  // The HTTP client applies the timeout to the connect and the header wait in turn.
  KOReaderSyncClient::setRequestTimeout(remaining / 2);
  return true;
}

std::string documentHash(const std::string& epubPath) {
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(epubPath)
             : KOReaderDocumentId::calculate(epubPath);
}

bool remoteIsBehind(const Deadline& deadline, const std::string& hash, const SavedProgressPosition& koPos) {
  if (!armRequest(deadline, "GET")) return false;

  KOReaderProgress remote{};
  const auto result = KOReaderSyncClient::getProgress(hash, remote);
  if (result == KOReaderSyncClient::NOT_FOUND) return true;
  if (result != KOReaderSyncClient::OK) {
    LOG_INF("AUTOSYNC", "Sleep sync GET failed (%s); not pushing", KOReaderSyncClient::errorString(result));
    return false;
  }
  if (koPos.percentage > remote.percentage) return true;

  LOG_INF("AUTOSYNC", "Server at %.4f is not behind local %.4f; not pushing", remote.percentage, koPos.percentage);
  return false;
}

bool pushProgress(const Deadline& deadline, const std::string& epubPath, const std::string& hash,
                  const CrossPointPosition& pos, const SavedProgressPosition& koPos) {
  if (!armRequest(deadline, "PUT")) return false;

  std::shared_ptr<Epub> epub;
  if (KOREADER_STORE.getSendMetadata()) {
    epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
    if (!epub->load(false, true)) {
      epub.reset();
    }
  }
  const KOReaderProgress progress = buildKOReaderUpload(epubPath, hash, koPos, pos, epub);
  epub.reset();

  const auto result = KOReaderSyncClient::updateProgress(progress);
  if (result == KOReaderSyncClient::OK) return true;

  LOG_INF("AUTOSYNC", "Sleep sync PUT failed (%s)", KOReaderSyncClient::errorString(result));
  return false;
}

void runSession(const Deadline& deadline, const std::string& epubPath, const CrossPointPosition& pos,
                const SavedProgressPosition& koPos, const SleepPush push, const std::optional<Marker>& marker) {
  if (!autosync::connectSavedWifi(deadline)) {
    LOG_INF("AUTOSYNC", "Sleep sync: no saved network joined");
    return;
  }

  const std::string hash = documentHash(epubPath);
  WifiPowerSaveGuard psGuard;
  if (push == SleepPush::CompareThenPush && !remoteIsBehind(deadline, hash, koPos)) return;
  if (!pushProgress(deadline, epubPath, hash, pos, koPos)) return;

  Marker synced;
  synced.spineIndex = static_cast<uint16_t>(pos.spineIndex);
  synced.pageNumber = static_cast<uint16_t>(pos.pageNumber);
  synced.percentage = koPos.percentage;
  synced.pulledThisSession = marker && marker->pulledThisSession;
  autosync::saveMarker(epubPath, synced);
}

bool networkAllowed = false;

}  // namespace

void setSleepSyncNetworkAllowed(const bool allowed) { networkAllowed = allowed; }

bool sleepSyncNetworkAllowed() { return networkAllowed; }

void syncBeforeSleep(const std::string& epubPath, const CrossPointPosition& pos, const SavedProgressPosition& koPos) {
  const auto marker = autosync::loadMarker(epubPath);
  const SleepPush push = autosync::decideSleepPush(marker, pos.spineIndex, pos.pageNumber);
  if (push == SleepPush::Skip) return;

  const uint32_t startMs = millis();
  const Deadline deadline(startMs, autosync::SESSION_BUDGET_MS - SLEEP_PATH_RESERVE_MS);
  LOG_INF("AUTOSYNC", "Sleep sync: %s", push == SleepPush::PushDirect ? "push" : "compare then push");

  runSession(deadline, epubPath, pos, koPos, push, marker);

  KOReaderSyncClient::setRequestTimeout(0);
  autosync::shutdownWifi();
  LOG_INF("AUTOSYNC", "Sleep sync done in %u ms", static_cast<unsigned>(millis() - startMs));
}
