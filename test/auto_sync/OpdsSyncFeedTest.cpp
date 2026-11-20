#include <gtest/gtest.h>

#include <string>

#include "OpdsSyncFeed.h"

using namespace autosync;

namespace {

const std::string SAMPLE_FEED = R"(<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom" xmlns:dc="http://purl.org/dc/terms/" xmlns:opds="http://opds-spec.org/2010/catalog">
    <id>urn:example:opds:books</id>
    <title>My Books</title>
    <updated>2026-11-19T20:07:19+07:00</updated>
    <author><name>example.com</name></author>
    <link rel="self" href="http://library.example.com/api/opds" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link rel="start" href="http://library.example.com/api/opds" type="application/atom+xml;profile=opds-catalog;kind=acquisition"/>
    <link rel="search" href="http://library.example.com/api/opds/search.xml" type="application/opensearchdescription+xml"/>
    <entry>
        <id>urn:example:book:1</id>
        <title>Sample Book</title>
        <author><name>Sample Author</name></author>
        <updated>2026-11-19T20:07:19+07:00</updated>
        <link rel="http://opds-spec.org/acquisition" href="http://library.example.com/api/opds/books/1/file" type="application/epub+zip" length="428448"/>
    </entry>
</feed>
)";

void expectSampleEntry(const OpdsSyncFeedParser& p) {
  ASSERT_EQ(p.entries().size(), 1u);
  const FeedEntry& e = p.entries()[0];
  EXPECT_EQ(e.id, "urn:example:book:1");
  EXPECT_EQ(e.title, "Sample Book");
  EXPECT_EQ(e.author, "Sample Author");
  EXPECT_EQ(e.updated, "2026-11-19T20:07:19+07:00");
  EXPECT_EQ(e.href, "http://library.example.com/api/opds/books/1/file");
  EXPECT_EQ(e.length, 428448u);
}

}  // namespace

TEST(OpdsSyncFeedTest, ParsesSampleFeed) {
  OpdsSyncFeedParser p;
  ASSERT_TRUE(p.feed(SAMPLE_FEED.data(), SAMPLE_FEED.size()));
  ASSERT_TRUE(p.finish());
  expectSampleEntry(p);
  EXPECT_TRUE(p.nextHref().empty());
}

TEST(OpdsSyncFeedTest, ChunkBoundaryIndependent) {
  OpdsSyncFeedParser p;
  for (char c : SAMPLE_FEED) {
    ASSERT_TRUE(p.feed(&c, 1));
  }
  ASSERT_TRUE(p.finish());
  expectSampleEntry(p);
}

TEST(OpdsSyncFeedTest, ReadsNextLink) {
  const std::string xml =
      R"(<feed xmlns="http://www.w3.org/2005/Atom"><link rel="next" href="/api/opds?page=2"/></feed>)";
  OpdsSyncFeedParser p;
  ASSERT_TRUE(p.feed(xml.data(), xml.size()));
  ASSERT_TRUE(p.finish());
  EXPECT_EQ(p.nextHref(), "/api/opds?page=2");
}

TEST(OpdsSyncFeedTest, SkipsIncompleteEntries) {
  const std::string xml = R"(<feed xmlns="http://www.w3.org/2005/Atom">
<entry><title>a</title><updated>u</updated>
 <link rel="http://opds-spec.org/acquisition" href="/a" type="application/epub+zip"/></entry>
<entry><id>b</id><title>b</title>
 <link rel="http://opds-spec.org/acquisition" href="/b" type="application/epub+zip"/></entry>
<entry><id>c</id><title>c</title><updated>u</updated>
 <link rel="http://opds-spec.org/acquisition" href="/c" type="application/pdf"/></entry>
</feed>)";
  OpdsSyncFeedParser p;
  ASSERT_TRUE(p.feed(xml.data(), xml.size()));
  ASSERT_TRUE(p.finish());
  EXPECT_TRUE(p.entries().empty());
}

TEST(OpdsSyncFeedTest, MalformedXmlFails) {
  const std::string xml = "<feed><entry></feed>";
  OpdsSyncFeedParser p;
  EXPECT_FALSE(p.feed(xml.data(), xml.size()) && p.finish());
}

TEST(ResolveHrefTest, Variants) {
  const std::string base = "http://h/api/opds";
  EXPECT_EQ(resolveHref(base, "/api/opds?page=2"), "http://h/api/opds?page=2");
  EXPECT_EQ(resolveHref(base, "http://x/y"), "http://x/y");
  EXPECT_EQ(resolveHref(base, "books/1/file"), "http://h/api/books/1/file");
}

TEST(SyncStateTest, RoundTrip) {
  const std::vector<SyncedBook> books = {
      {"urn:b:1", "2026-11-19T20:07:19+07:00", "/Books/Sách mẫu - Tác giả mẫu.epub"},
      {"urn:b:2", "2026-11-20T01:00:00+07:00", "/Books/b.epub"},
  };
  const auto parsed = parseSyncState(serializeSyncState(books));
  ASSERT_EQ(parsed.size(), 2u);
  for (size_t i = 0; i < books.size(); i++) {
    EXPECT_EQ(parsed[i].id, books[i].id);
    EXPECT_EQ(parsed[i].updated, books[i].updated);
    EXPECT_EQ(parsed[i].path, books[i].path);
  }
}

TEST(PlanBookTest, NoPreviousAndMissingSkips) {
  FeedEntry e;
  e.updated = "u1";
  e.length = 100;
  EXPECT_EQ(planBook(e, nullptr, "/a.epub", std::nullopt, false), BookAction::Skip);
}

TEST(PlanBookTest, NoPreviousAndSameSizeAdopts) {
  FeedEntry e;
  e.updated = "u1";
  e.length = 100;
  EXPECT_EQ(planBook(e, nullptr, "/a.epub", 100, false), BookAction::Adopt);
}

TEST(PlanBookTest, NoPreviousAndDifferentSizeRedownloads) {
  FeedEntry e;
  e.updated = "u1";
  e.length = 100;
  EXPECT_EQ(planBook(e, nullptr, "/a.epub", 99, false), BookAction::Redownload);
}

TEST(PlanBookTest, NoPreviousAndUnknownLengthRedownloads) {
  FeedEntry e;
  e.updated = "u1";
  EXPECT_EQ(planBook(e, nullptr, "/a.epub", 100, false), BookAction::Redownload);
}

TEST(PlanBookTest, PreviousFileDeletedForgets) {
  FeedEntry e;
  e.updated = "u2";
  const SyncedBook prev{"id", "u1", "/a.epub"};
  EXPECT_EQ(planBook(e, &prev, "/a.epub", std::nullopt, false), BookAction::Forget);
}

TEST(PlanBookTest, PathChangedMovesAndRedownloads) {
  FeedEntry e;
  e.updated = "u1";
  const SyncedBook prev{"id", "u1", "/old.epub"};
  EXPECT_EQ(planBook(e, &prev, "/a.epub", std::nullopt, true), BookAction::MoveAndRedownload);
}

TEST(PlanBookTest, UpdatedChangedRedownloads) {
  FeedEntry e;
  e.updated = "u2";
  const SyncedBook prev{"id", "u1", "/a.epub"};
  EXPECT_EQ(planBook(e, &prev, "/a.epub", 100, true), BookAction::Redownload);
}

TEST(PlanBookTest, UnchangedSkips) {
  FeedEntry e;
  e.updated = "u1";
  const SyncedBook prev{"id", "u1", "/a.epub"};
  EXPECT_EQ(planBook(e, &prev, "/a.epub", 100, true), BookAction::Skip);
}
