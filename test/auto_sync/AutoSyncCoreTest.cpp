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

constexpr uint32_t WINDOW = 15 * 60;

TEST(RecentReadTest, InsideWindowIsRecent) {
  EXPECT_TRUE(readRecently(1'800'000'000, 1'800'000'000, WINDOW));
  EXPECT_TRUE(readRecently(1'800'000'000 + WINDOW - 1, 1'800'000'000, WINDOW));
}

TEST(RecentReadTest, WindowEndIsNotRecent) {
  EXPECT_FALSE(readRecently(1'800'000'000 + WINDOW, 1'800'000'000, WINDOW));
}

TEST(RecentReadTest, ZeroWindowNeverSkips) { EXPECT_FALSE(readRecently(1'800'000'000, 1'800'000'000, 0)); }

TEST(RecentReadTest, UnknownOrBackwardClockIsNotRecent) {
  EXPECT_FALSE(readRecently(0, 1'800'000'000, WINDOW));
  EXPECT_FALSE(readRecently(1'800'000'000, 0, WINDOW));
  EXPECT_FALSE(readRecently(1'800'000'000, 1'800'000'060, WINDOW));
}

TEST(SkipMinutesTest, CyclesThroughChoices) {
  EXPECT_EQ(nextSkipMinutes(0), 5);
  EXPECT_EQ(nextSkipMinutes(5), 10);
  EXPECT_EQ(nextSkipMinutes(10), 15);
  EXPECT_EQ(nextSkipMinutes(15), 0);
  EXPECT_EQ(nextSkipMinutes(7), 0);
}

TEST(SkipMinutesTest, OnlyListedValuesAreChoices) {
  EXPECT_TRUE(isSkipMinutesChoice(0));
  EXPECT_TRUE(isSkipMinutesChoice(15));
  EXPECT_FALSE(isSkipMinutesChoice(7));
  EXPECT_TRUE(isSkipMinutesChoice(DEFAULT_SKIP_MINUTES));
}

TEST(SkipMinutesTest, WindowUsesDefaultForUnknownValues) {
  EXPECT_EQ(skipWindowSeconds(0), 0u);
  EXPECT_EQ(skipWindowSeconds(10), 600u);
  EXPECT_EQ(skipWindowSeconds(7), DEFAULT_SKIP_MINUTES * 60u);
}
