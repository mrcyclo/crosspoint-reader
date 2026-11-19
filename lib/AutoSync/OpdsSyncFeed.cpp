#include "OpdsSyncFeed.h"

#include <expat.h>

namespace autosync {

namespace {

constexpr const char* ACQUISITION_REL = "http://opds-spec.org/acquisition";
constexpr const char* EPUB_TYPE = "application/epub+zip";

std::string localName(const char* name) {
  std::string s(name);
  const size_t pos = s.find_last_of(":|");
  if (pos == std::string::npos) return s;
  return s.substr(pos + 1);
}

std::string trim(const std::string& s) {
  const char* ws = " \t\r\n";
  const size_t b = s.find_first_not_of(ws);
  if (b == std::string::npos) return "";
  const size_t e = s.find_last_not_of(ws);
  return s.substr(b, e - b + 1);
}

std::string attr(const char** attrs, const char* key) {
  for (int i = 0; attrs[i]; i += 2) {
    if (localName(attrs[i]) == key) return attrs[i + 1];
  }
  return "";
}

}  // namespace

OpdsSyncFeedParser::OpdsSyncFeedParser() {
  XML_Parser p = XML_ParserCreate(nullptr);
  parser = p;
  if (!p) {
    failed = true;
    return;
  }
  XML_SetUserData(p, this);
  XML_SetElementHandler(p, onStart, onEnd);
  XML_SetCharacterDataHandler(p, onText);
}

OpdsSyncFeedParser::~OpdsSyncFeedParser() {
  if (parser) XML_ParserFree(static_cast<XML_Parser>(parser));
}

bool OpdsSyncFeedParser::feed(const char* data, size_t len) {
  if (failed) return false;
  if (XML_Parse(static_cast<XML_Parser>(parser), data, static_cast<int>(len), 0) == XML_STATUS_ERROR) {
    failed = true;
    return false;
  }
  return true;
}

bool OpdsSyncFeedParser::finish() {
  if (failed) return false;
  if (XML_Parse(static_cast<XML_Parser>(parser), "", 0, 1) == XML_STATUS_ERROR) {
    failed = true;
    return false;
  }
  return true;
}

void OpdsSyncFeedParser::onStart(void* user, const char* name, const char** attrs) {
  static_cast<OpdsSyncFeedParser*>(user)->handleStart(name, attrs);
}

void OpdsSyncFeedParser::onEnd(void* user, const char* name) {
  static_cast<OpdsSyncFeedParser*>(user)->handleEnd(name);
}

void OpdsSyncFeedParser::onText(void* user, const char* s, int len) {
  static_cast<OpdsSyncFeedParser*>(user)->text.append(s, static_cast<size_t>(len));
}

void OpdsSyncFeedParser::handleStart(const char* name, const char** attrs) {
  const std::string local = localName(name);
  path.push_back(local);
  text.clear();

  if (path.size() == 2 && local == "entry") {
    current = FeedEntry();
    hasEpubLink = false;
    return;
  }
  if (local != "link") return;

  const std::string rel = attr(attrs, "rel");
  if (path.size() == 2 && rel == "next") {
    next = attr(attrs, "href");
    return;
  }
  if (path.size() != 3 || path[1] != "entry") return;
  if (rel != ACQUISITION_REL || attr(attrs, "type") != EPUB_TYPE) return;
  if (hasEpubLink) return;
  hasEpubLink = true;
  current.href = attr(attrs, "href");
}

void OpdsSyncFeedParser::handleEnd(const char*) {
  const std::string local = path.back();
  const size_t depth = path.size();
  path.pop_back();

  if (depth == 2 && local == "entry") {
    if (current.id.empty() || current.updated.empty() || !hasEpubLink || current.href.empty()) return;
    books.push_back(current);
    return;
  }
  if (depth < 3 || path[1] != "entry") return;

  if (depth == 3) {
    if (local == "id") current.id = trim(text);
    if (local == "title") current.title = trim(text);
    if (local == "updated") current.updated = trim(text);
    return;
  }
  if (depth == 4 && local == "name" && path[2] == "author") {
    current.author = trim(text);
  }
}

std::string resolveHref(const std::string& baseUrl, const std::string& href) {
  if (href.find("://") != std::string::npos) return href;

  const size_t schemeEnd = baseUrl.find("://");
  if (schemeEnd == std::string::npos) return href;
  const size_t hostEnd = baseUrl.find('/', schemeEnd + 3);
  const std::string origin = hostEnd == std::string::npos ? baseUrl : baseUrl.substr(0, hostEnd);

  if (!href.empty() && href[0] == '/') return origin + href;
  if (hostEnd == std::string::npos) return origin + "/" + href;

  const size_t queryPos = baseUrl.find_first_of("?#", hostEnd);
  const size_t pathLen = queryPos == std::string::npos ? std::string::npos : queryPos - hostEnd;
  const std::string basePath = baseUrl.substr(hostEnd, pathLen);
  const size_t lastSlash = basePath.find_last_of('/');
  return origin + basePath.substr(0, lastSlash + 1) + href;
}

std::vector<SyncedBook> parseSyncState(const std::string& text) {
  std::vector<SyncedBook> out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    const size_t t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    const size_t t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;

    SyncedBook book{line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1), line.substr(t2 + 1)};
    if (book.id.empty() || book.path.empty()) continue;
    out.push_back(std::move(book));
  }
  return out;
}

std::string serializeSyncState(const std::vector<SyncedBook>& books) {
  std::string out;
  for (const auto& b : books) {
    out += b.id + '\t' + b.updated + '\t' + b.path + '\n';
  }
  return out;
}

BookAction planBook(const FeedEntry& entry, const SyncedBook* previous, const std::string& destPath, bool destExists) {
  if (!previous) return destExists ? BookAction::Skip : BookAction::Download;
  if (previous->path != destPath) return BookAction::MoveAndRedownload;
  if (previous->updated != entry.updated) return BookAction::Redownload;
  return BookAction::Skip;
}

}  // namespace autosync
