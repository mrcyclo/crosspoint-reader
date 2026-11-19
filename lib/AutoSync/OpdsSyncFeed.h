#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace autosync {

struct FeedEntry {
  std::string id, title, author, href, updated;
};

// Streaming parser: feed() accepts arbitrary chunk boundaries.
class OpdsSyncFeedParser {
 public:
  OpdsSyncFeedParser();
  ~OpdsSyncFeedParser();
  OpdsSyncFeedParser(const OpdsSyncFeedParser&) = delete;
  OpdsSyncFeedParser& operator=(const OpdsSyncFeedParser&) = delete;

  bool feed(const char* data, size_t len);
  bool finish();
  const std::vector<FeedEntry>& entries() const { return books; }
  const std::string& nextHref() const { return next; }

 private:
  static void onStart(void* user, const char* name, const char** attrs);
  static void onEnd(void* user, const char* name);
  static void onText(void* user, const char* s, int len);
  void handleStart(const char* name, const char** attrs);
  void handleEnd(const char* name);

  void* parser = nullptr;
  bool failed = false;
  std::vector<std::string> path;
  std::string text;
  FeedEntry current;
  bool hasEpubLink = false;
  std::vector<FeedEntry> books;
  std::string next;
};

std::string resolveHref(const std::string& baseUrl, const std::string& href);

struct SyncedBook {
  std::string id, updated, path;
};

std::vector<SyncedBook> parseSyncState(const std::string& text);
std::string serializeSyncState(const std::vector<SyncedBook>& books);
constexpr const char* SYNC_STATE_FILE = "/.crosspoint/opds-sync.tsv";

enum class BookAction { Skip, Download, Redownload, MoveAndRedownload };
BookAction planBook(const FeedEntry& entry, const SyncedBook* previous, const std::string& destPath, bool destExists);

}  // namespace autosync
