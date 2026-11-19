#include <gtest/gtest.h>

#include <string>

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

TEST(MarkerTest, RoundTrip) {
  Marker m;
  m.spineIndex = 3;
  m.pageNumber = 41;
  m.percentage = 0.25f;
  m.pulledThisSession = true;
  const std::string bytes = encodeMarker(m);
  ASSERT_EQ(bytes.size(), 10u);
  const auto decoded = decodeMarker(bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->spineIndex, 3);
  EXPECT_EQ(decoded->pageNumber, 41);
  EXPECT_FLOAT_EQ(decoded->percentage, 0.25f);
  EXPECT_TRUE(decoded->pulledThisSession);
}

TEST(MarkerTest, RejectsWrongSizeOrVersion) {
  EXPECT_FALSE(decodeMarker("").has_value());
  EXPECT_FALSE(decodeMarker(std::string(9, '\x01')).has_value());
  std::string bytes = encodeMarker(Marker{});
  bytes[0] = 2;
  EXPECT_FALSE(decodeMarker(bytes).has_value());
}

TEST(SleepPushDecisionTest, SamePositionSkips) {
  Marker m;
  m.spineIndex = 2;
  m.pageNumber = 7;
  m.pulledThisSession = true;
  EXPECT_EQ(decideSleepPush(m, 2, 7), SleepPush::Skip);
}

TEST(SleepPushDecisionTest, PulledThisSessionPushesDirect) {
  Marker m;
  m.spineIndex = 2;
  m.pageNumber = 7;
  m.pulledThisSession = true;
  EXPECT_EQ(decideSleepPush(m, 2, 8), SleepPush::PushDirect);
}

TEST(SleepPushDecisionTest, NotPulledComparesFirst) {
  Marker m;
  m.spineIndex = 2;
  m.pageNumber = 7;
  EXPECT_EQ(decideSleepPush(m, 3, 0), SleepPush::CompareThenPush);
}

TEST(SleepPushDecisionTest, NoMarkerComparesFirst) {
  EXPECT_EQ(decideSleepPush(std::nullopt, 0, 0), SleepPush::CompareThenPush);
}

TEST(AutoSyncPolicyTest, SkipsOnSilentBoot) {
  EXPECT_FALSE(shouldSyncOnOpen({true, true, true, true}));
}

TEST(AutoSyncPolicyTest, SkipsWhenDisabled) {
  EXPECT_FALSE(shouldSyncOnOpen({false, true, false, true}));
  EXPECT_FALSE(shouldSyncOnOpen({true, false, false, true}));
  EXPECT_FALSE(shouldSyncOnSleep(false, true, true));
}

TEST(AutoSyncPolicyTest, SyncsOnNormalOpen) {
  EXPECT_TRUE(shouldSyncOnOpen({true, true, false, true}));
}
