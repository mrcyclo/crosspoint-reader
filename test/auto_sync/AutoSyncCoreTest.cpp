#include <gtest/gtest.h>

#include "AutoSyncCore.h"

using namespace autosync;

TEST(DeadlineTest, ExpiresAfterBudget) {
  Deadline d(1000, 15000);
  EXPECT_FALSE(d.expired(15999));
  EXPECT_TRUE(d.expired(16000));
}

TEST(DeadlineTest, WrapSafe) {
  Deadline d(0xFFFFF000u, 15000);
  EXPECT_FALSE(d.expired(0x00002000u));
  EXPECT_TRUE(d.expired(0x00003000u));
}

TEST(DeadlineTest, RemainingIsZeroWhenExpired) {
  Deadline d(1000, 15000);
  EXPECT_EQ(d.remaining(1000), 15000u);
  EXPECT_EQ(d.remaining(11000), 5000u);
  EXPECT_EQ(d.remaining(16000), 0u);
  EXPECT_EQ(d.remaining(99999), 0u);
}

TEST(AutoSyncPolicyTest, SkipsOnSilentBoot) { EXPECT_FALSE(shouldSyncOnOpen({true, true, true, false})); }

TEST(AutoSyncPolicyTest, SkipsWhenDisabled) {
  EXPECT_FALSE(shouldSyncOnOpen({false, true, false, false}));
  EXPECT_FALSE(shouldSyncOnOpen({true, false, false, false}));
}

TEST(AutoSyncPolicyTest, SyncsOnNormalOpen) { EXPECT_TRUE(shouldSyncOnOpen({true, true, false, false})); }

TEST(AutoSyncPolicyTest, SkipsWhenReadRecently) { EXPECT_FALSE(shouldSyncOnOpen({true, true, false, true})); }

TEST(RecentReadTest, InsideWindowIsRecent) {
  EXPECT_TRUE(readRecently(1'800'000'000, 1'800'000'000));
  EXPECT_TRUE(readRecently(1'800'000'000 + RECENT_READ_SECONDS - 1, 1'800'000'000));
}

TEST(RecentReadTest, WindowEndIsNotRecent) {
  EXPECT_FALSE(readRecently(1'800'000'000 + RECENT_READ_SECONDS, 1'800'000'000));
}

TEST(RecentReadTest, UnknownOrBackwardClockIsNotRecent) {
  EXPECT_FALSE(readRecently(0, 1'800'000'000));
  EXPECT_FALSE(readRecently(1'800'000'000, 0));
  EXPECT_FALSE(readRecently(1'800'000'000, 1'800'000'060));
}
