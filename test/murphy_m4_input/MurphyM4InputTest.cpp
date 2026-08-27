#include <gtest/gtest.h>

#include "MurphyM4TouchPolling.h"

namespace touch = freeink::murphy_m4_touch;

TEST(MurphyM4Input, RetainsCompleteGestureBetweenMainLoopUpdates) {
  touch::PollState state;
  touch::recordContact(state, 40, 100, 1000);
  touch::recordContact(state, 330, 150, 1040);
  touch::recordRelease(state, 1080);

  const auto snapshot = touch::takeSnapshot(state);
  EXPECT_FALSE(snapshot.contact);
  ASSERT_TRUE(snapshot.completedPending);
  EXPECT_EQ(snapshot.completedDown.x, 40);
  EXPECT_EQ(snapshot.completedDown.y, 100);
  EXPECT_EQ(snapshot.completedDown.timestamp, 1000U);
  EXPECT_EQ(snapshot.completedUp.x, 330);
  EXPECT_EQ(snapshot.completedUp.y, 150);
  EXPECT_EQ(snapshot.completedUp.timestamp, 1080U);
  EXPECT_FALSE(touch::takeSnapshot(state).completedPending);
}

TEST(MurphyM4Input, KeepsCurrentContactWithoutCompletingIt) {
  touch::PollState state;
  touch::recordContact(state, 50, 60, 2000);
  touch::recordContact(state, 55, 65, 2010);

  const auto snapshot = touch::takeSnapshot(state);
  EXPECT_TRUE(snapshot.contact);
  EXPECT_FALSE(snapshot.completedPending);
  EXPECT_EQ(snapshot.down.x, 50);
  EXPECT_EQ(snapshot.down.timestamp, 2000U);
  EXPECT_EQ(snapshot.latest.x, 55);
  EXPECT_EQ(snapshot.latest.timestamp, 2010U);
}

TEST(MurphyM4Input, RetainsFirstUnconsumedCompletedGesture) {
  touch::PollState state;
  touch::recordContact(state, 10, 20, 100);
  touch::recordRelease(state, 120);
  touch::recordContact(state, 30, 40, 130);
  touch::recordRelease(state, 150);

  const auto snapshot = touch::takeSnapshot(state);
  ASSERT_TRUE(snapshot.completedPending);
  EXPECT_EQ(snapshot.completedDown.x, 10);
  EXPECT_EQ(snapshot.completedUp.x, 10);
  EXPECT_EQ(snapshot.completedUp.timestamp, 120U);
}

TEST(MurphyM4Input, ReleasesAContactAfterStaleReadFailures) {
  touch::PollState state;
  touch::recordContact(state, 70, 80, 500);

  EXPECT_FALSE(touch::releaseIfStale(state, 599, 100));
  EXPECT_TRUE(touch::releaseIfStale(state, 600, 100));
  const auto snapshot = touch::takeSnapshot(state);
  ASSERT_TRUE(snapshot.completedPending);
  EXPECT_EQ(snapshot.completedDown.timestamp, 500U);
  EXPECT_EQ(snapshot.completedUp.timestamp, 600U);
}

TEST(MurphyM4Input, RejectsInvalidFramesWithoutCreatingAContact) {
  EXPECT_EQ(touch::classifyFrame(0x10, 0, 0, 0, 0), touch::FrameState::Invalid);
  EXPECT_EQ(touch::classifyFrame(1, 1, 1, 1, 1), touch::FrameState::Invalid);
  EXPECT_EQ(touch::classifyFrame(1, 0xC1, 0x23, 0, 0x42), touch::FrameState::Invalid);

  touch::PollState state;
  EXPECT_FALSE(touch::releaseIfStale(state, 1000, 100));
  EXPECT_FALSE(touch::takeSnapshot(state).completedPending);
}
