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

TEST(AutoSyncPolicyTest, SkipsOnSilentBoot) { EXPECT_FALSE(shouldSyncOnOpen({true, true, true})); }

TEST(AutoSyncPolicyTest, SkipsWhenDisabled) {
  EXPECT_FALSE(shouldSyncOnOpen({false, true, false}));
  EXPECT_FALSE(shouldSyncOnOpen({true, false, false}));
}

TEST(AutoSyncPolicyTest, SyncsOnNormalOpen) { EXPECT_TRUE(shouldSyncOnOpen({true, true, false})); }
