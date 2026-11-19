#pragma once

#include <OpdsSyncFeed.h>

#include <string>
#include <vector>

#include "activities/Activity.h"

// Library "Sync Books": mirrors the first OPDS server's acquisition feed onto
// the card. Entries are matched by id against SYNC_STATE_FILE; new ids are
// downloaded, a changed `updated` re-downloads, and a changed destination path
// (renamed title or author) downloads to the new path and removes the old file.
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
  int added = 0;
  int updated = 0;
  int failed = 0;
  std::string currentTitle;

  void launchWifiSelection();
  void startSync();
  bool fetchFeed();
  void planJobs();
  void runNextJob();
  bool downloadBook(const Job& job);
  void recordBook(const autosync::FeedEntry& entry, const std::string& path);
  void writeState() const;
  void pollCancel(bool refreshInput);
  void finishSync();
  void exitWithResult();
};
