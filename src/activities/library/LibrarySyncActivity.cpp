#include "LibrarySyncActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"

namespace {

constexpr int MAX_FEED_PAGES = 50;
constexpr size_t MAX_STATE_BYTES = 64 * 1024;

// The state TSV has no escaping, so a tab or newline in any field would split the row.
bool tsvSafe(const std::string& value) { return value.find_first_of("\t\n\r") == std::string::npos; }

bool isEpubFile(const std::string& path) {
  uint8_t magic[4] = {0};
  HalFile file;
  if (!Storage.openFileForRead("SYNC", path.c_str(), file)) return false;
  file.read(magic, sizeof(magic));
  file.close();
  return magic[0] == 'P'
      && magic[1] == 'K'
      && magic[2] == 3
      && magic[3] == 4;
}

}  // namespace

void LibrarySyncActivity::onEnter() {
  Activity::onEnter();
  if (OPDS_STORE.getCount() == 0) {
    state = State::NoServer;
    requestUpdate();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    startSync();
    return;
  }
  tearDownWifiOnExit = true;
  launchWifiSelection();
}

void LibrarySyncActivity::onExit() {
  Activity::onExit();
  if (!tearDownWifiOnExit || WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(false);
  delay(30);
  silentRestart();
}

void LibrarySyncActivity::launchWifiSelection() {
  state = State::WifiSelection;
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          exitWithResult();
          return;
        }
        startSync();
      }
  );
}

void LibrarySyncActivity::startSync() {
  {
    RenderLock lock(*this);
    state = State::Loading;
  }
  requestUpdate(true);
}

void LibrarySyncActivity::loop() {
  if (state == State::WifiSelection) return;

  if (state == State::Loading) {
    requestUpdateAndWait();
    if (!fetchFeed()) {
      RenderLock lock(*this);
      state = State::Failed;
      requestUpdate();
      return;
    }
    planJobs();
    RenderLock lock(*this);
    state = State::Downloading;
    return;
  }

  if (state == State::Downloading) {
    pollCancel(false);
    if (cancelRequested || nextJob >= jobs.size()) {
      finishSync();
      return;
    }
    runNextJob();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)
      || mappedInput.wasReleased(MappedInputManager::Button::Confirm)
      || mappedInput.wasScreenTapped(x, y)) {
    exitWithResult();
  }
}

bool LibrarySyncActivity::fetchFeed() {
  const OpdsServer& server = OPDS_STORE.getServers().front();
  const std::vector<HttpDownloader::Header> headers = server.activeCustomHeaders();
  std::string url = server.url;
  for (int page = 0; page < MAX_FEED_PAGES && !url.empty(); ++page) {
    autosync::OpdsSyncFeedParser parser;
    bool parseOk = true;
    const bool fetched = HttpDownloader::fetchUrl(
        url,
        [&parser, &parseOk](const uint8_t* data, const size_t len) {
          parseOk = parser.feed(reinterpret_cast<const char*>(data), len);
          return parseOk;
        },
        server.username,
        server.password,
        headers
    );
    if (!fetched || !parseOk || !parser.finish()) {
      LOG_ERR("SYNC", "Feed page %d failed (HTTP %d): %s", page + 1, HttpDownloader::lastStatus(), url.c_str());
      return false;
    }
    for (const auto& entry : parser.entries()) {
      autosync::FeedEntry resolved = entry;
      resolved.href = autosync::resolveHref(url, entry.href);
      entries.push_back(std::move(resolved));
    }
    url = parser.nextHref().empty() ? std::string() : autosync::resolveHref(url, parser.nextHref());
  }
  if (!url.empty()) {
    LOG_ERR("SYNC", "Feed paging stopped after %d pages", MAX_FEED_PAGES);
  }
  LOG_INF("SYNC", "Feed has %u books", static_cast<unsigned>(entries.size()));
  return true;
}

void LibrarySyncActivity::planJobs() {
  std::string text;
  if (Storage.exists(autosync::SYNC_STATE_FILE)
      && Storage.readFileToString("SYNC", autosync::SYNC_STATE_FILE, MAX_STATE_BYTES, text)) {
    synced = autosync::parseSyncState(text);
  }

  bool adopted = false;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    const std::string dest = opdsDownloadPath(entry.author, entry.title);
    if (!tsvSafe(entry.id) || !tsvSafe(entry.updated) || !tsvSafe(dest)) {
      LOG_ERR("SYNC", "Unstorable entry skipped: %s", entry.id.c_str());
      ++failed;
      continue;
    }
    const autosync::SyncedBook* previous = nullptr;
    for (const auto& book : synced) {
      if (book.id == entry.id) {
        previous = &book;
        break;
      }
    }
    const bool destExists = Storage.exists(dest.c_str());
    const autosync::BookAction action = autosync::planBook(entry, previous, dest, destExists);
    if (action != autosync::BookAction::Skip) {
      jobs.push_back({i, action, dest, previous ? previous->path : std::string()});
      continue;
    }
    if (!previous) {
      // Already on the card from an earlier manual download: track it from now on.
      recordBook(entry, dest);
      adopted = true;
    }
  }
  if (adopted) {
    writeState();
  }
  LOG_INF("SYNC", "%u to download", static_cast<unsigned>(jobs.size()));
}

void LibrarySyncActivity::runNextJob() {
  const Job& job = jobs[nextJob];
  const auto& entry = entries[job.entry];
  {
    RenderLock lock(*this);
    currentTitle = entry.title;
  }
  requestUpdateAndWait();

  const bool ok = downloadBook(job);
  ++nextJob;
  if (cancelRequested) return;
  if (!ok) {
    ++failed;
    return;
  }

  if (job.action == autosync::BookAction::MoveAndRedownload
      && Storage.exists(job.previousPath.c_str())
      && !removeBookFile(job.previousPath)) {
    LOG_ERR("SYNC", "Could not remove old copy %s", job.previousPath.c_str());
  }
  if (job.action == autosync::BookAction::Download) {
    ++added;
  } else {
    ++updated;
  }
  recordBook(entry, job.dest);
  writeState();
}

// Stages into "<dest>.sync" so a failed or non-EPUB answer never replaces the copy on the card.
bool LibrarySyncActivity::downloadBook(const Job& job) {
  const OpdsServer& server = OPDS_STORE.getServers().front();
  const auto& entry = entries[job.entry];
  const std::string staged = job.dest + ".sync";

  // SD font caches can hold tens of KB the TLS session needs; they repopulate on demand.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP
      || ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("SYNC", "Low heap for download (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }

  LOG_INF("SYNC", "Downloading %s -> %s", entry.href.c_str(), job.dest.c_str());
  const auto result = HttpDownloader::downloadToFile(
      entry.href,
      staged,
      [this](size_t, size_t) { pollCancel(true); },
      &cancelRequested,
      server.username,
      server.password,
      server.activeCustomHeaders()
  );
  if (result != HttpDownloader::OK) {
    LOG_ERR("SYNC", "Download failed (%d): %s", static_cast<int>(result), entry.title.c_str());
    return false;
  }
  if (!isEpubFile(staged)) {
    LOG_ERR("SYNC", "Not an EPUB: %s", entry.title.c_str());
    Storage.remove(staged.c_str());
    return false;
  }
  if (!Storage.replaceFile(staged.c_str(), job.dest.c_str())) {
    LOG_ERR("SYNC", "Could not move download into place: %s", job.dest.c_str());
    Storage.remove(staged.c_str());
    return false;
  }
  clearBookCache(job.dest);
  return true;
}

void LibrarySyncActivity::recordBook(const autosync::FeedEntry& entry, const std::string& path) {
  for (auto& book : synced) {
    if (book.id == entry.id) {
      book.updated = entry.updated;
      book.path = path;
      return;
    }
  }
  synced.push_back({entry.id, entry.updated, path});
}

void LibrarySyncActivity::writeState() const {
  const std::string tmp = std::string(autosync::SYNC_STATE_FILE) + ".tmp";
  const std::string text = autosync::serializeSyncState(synced);
  HalFile file;
  if (!Storage.openFileForWrite("SYNC", tmp.c_str(), file)) {
    LOG_ERR("SYNC", "Cannot write %s", tmp.c_str());
    return;
  }
  const bool written = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
  file.close();
  if (!written || !Storage.replaceFile(tmp.c_str(), autosync::SYNC_STATE_FILE)) {
    LOG_ERR("SYNC", "Cannot replace %s", autosync::SYNC_STATE_FILE);
    Storage.remove(tmp.c_str());
  }
}

// refreshInput: called from inside a blocking transfer, where the activity loop cannot update input.
void LibrarySyncActivity::pollCancel(const bool refreshInput) {
  if (cancelRequested) return;
  if (refreshInput) {
    mappedInput.update(true);
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)
      || mappedInput.wasBackGesture()
      || mappedInput.wasScreenTapped(x, y)) {
    LOG_INF("SYNC", "Cancelled by user");
    cancelRequested = true;
  }
}

void LibrarySyncActivity::finishSync() {
  const char* suffix = cancelRequested ? " (cancelled)" : "";
  LOG_INF("SYNC", "Done: %d new, %d updated, %d failed%s", added, updated, failed, suffix);
  if (added + updated > 0) {
    library::markLibraryIndexDirty();
  }
  RenderLock lock(*this);
  state = State::Done;
  requestUpdate(true);
}

void LibrarySyncActivity::exitWithResult() {
  setResult(SyncBooksResult{added + updated > 0});
  finish();
}

void LibrarySyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int midY = renderer.getScreenHeight() / 2;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SYNC_BOOKS));

  char line[96];
  const char* hint = tr(STR_BACK);
  switch (state) {
    case State::NoServer:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_SYNC_BOOKS_NO_SERVER), true, EpdFontFamily::BOLD);
      break;
    case State::WifiSelection:
    case State::Loading:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_LOADING));
      hint = "";
      break;
    case State::Downloading: {
      const int total = static_cast<int>(jobs.size());
      snprintf(line, sizeof(line), tr(STR_SYNC_BOOKS_DOWNLOADING), static_cast<int>(nextJob + 1), total);
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, line, true, EpdFontFamily::BOLD);
      const std::string title =
          renderer.truncatedText(UI_10_FONT_ID, currentTitle.c_str(), pageWidth - 2 * metrics.contentSidePadding);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, title.c_str());
      hint = tr(STR_CANCEL);
      break;
    }
    case State::Failed:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_ERROR_MSG), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_DOWNLOAD_FAILED));
      break;
    case State::Done:
      snprintf(line, sizeof(line), tr(STR_SYNC_BOOKS_SUMMARY), added, updated, failed);
      renderer.drawCenteredText(UI_12_FONT_ID, midY, line, true, EpdFontFamily::BOLD);
      break;
  }

  const auto labels = mappedInput.mapLabels(hint, "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
