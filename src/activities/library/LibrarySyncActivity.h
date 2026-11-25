#pragma once

#include <OpdsSyncFeed.h>

#include <string>
#include <vector>

#include "activities/Activity.h"

// Library "Sync Books": refreshes the books already on the card from the first
// OPDS server's acquisition feed; books not on the card are never downloaded.
// Entries are matched by id against SYNC_STATE_FILE; a changed `updated`
// re-downloads, and a changed destination path (renamed title or author)
// downloads to the new path and removes the old file. See autosync::planBook.
// Finishes with SyncBooksResult{changed}; when it brought Wi-Fi up it exits
// through silentRestart() instead.
class LibrarySyncActivity final : public Activity {
 public:
  LibrarySyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibrarySync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return state == State::Downloading; }
  bool preventAutoSleep() override { return state == State::Loading || state == State::Downloading; }
  void render(RenderLock&&) override;

 private:
  enum class State { NoServer, WifiSelection, Loading, Downloading, Failed, Done };

  struct Job {
    size_t entry;
    autosync::BookAction action;
    std::string dest;
    std::string previousPath;
  };

  State state = State::Loading;
  bool tearDownWifiOnExit = false;
  bool cancelRequested = false;

  std::vector<autosync::FeedEntry> entries;
  std::vector<autosync::SyncedBook> synced;
  std::vector<Job> jobs;
  size_t nextJob = 0;
  int updated = 0;
  int failed = 0;
  std::string currentTitle;

  void launchWifiSelection();
  void startSync();
  bool fetchFeed();
  void planJobs();
  void runNextJob();
  bool downloadBook(const Job& job);
  autosync::SyncedBook* findSynced(const std::string& id);
  void recordBook(const autosync::FeedEntry& entry, const std::string& path);
  void forgetBook(const std::string& id);
  void writeState() const;
  void pollCancel(bool refreshInput);
  void finishSync();
  void exitWithResult();
};
