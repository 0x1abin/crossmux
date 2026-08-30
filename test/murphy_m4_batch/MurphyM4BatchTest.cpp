#include <gtest/gtest.h>

#include "MurphyM4Batch.h"
#include "MurphyM4BatchPreference.h"

TEST(MurphyM4Batch, PersistsExplicitValuesAndDefaultsToSecond) {
  using freeink::MurphyM4Batch;
  EXPECT_EQ(MurphyM4BatchPreference::encode(MurphyM4Batch::First), 1);
  EXPECT_EQ(MurphyM4BatchPreference::encode(MurphyM4Batch::Second), 2);
  EXPECT_EQ(MurphyM4BatchPreference::decode(1), MurphyM4Batch::First);
  EXPECT_EQ(MurphyM4BatchPreference::decode(2), MurphyM4Batch::Second);
  EXPECT_EQ(MurphyM4BatchPreference::decode(0), MurphyM4Batch::Second);
  EXPECT_EQ(MurphyM4BatchPreference::decode(255), MurphyM4Batch::Second);
}

TEST(MurphyM4Batch, AppliesReferenceTouchCalibration) {
  using freeink::MurphyM4Batch;
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(40, MurphyM4Batch::First, 479), 73, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(330, MurphyM4Batch::First, 479), 303, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(389, MurphyM4Batch::First, 479), 349, 1);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(553, MurphyM4Batch::First, 479), 479);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(514, MurphyM4Batch::Second, 479), 479);
}
