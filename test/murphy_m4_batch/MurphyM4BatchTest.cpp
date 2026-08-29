#include <gtest/gtest.h>

#include <array>

#include "MurphyM4Batch.h"
#include "MurphyM4BatchDetection.h"

TEST(MurphyM4Batch, ConfirmsOnlyMatchedFirstBatchChannels) {
  EXPECT_TRUE(MurphyM4BatchDetection::isFirstBatch(6008, 6050));
  EXPECT_TRUE(MurphyM4BatchDetection::isFirstBatch(5200, 5200));
  EXPECT_TRUE(MurphyM4BatchDetection::isFirstBatch(10000, 10000));
  EXPECT_TRUE(MurphyM4BatchDetection::isFirstBatch(5200, 6500));
  EXPECT_TRUE(MurphyM4BatchDetection::isFirstBatch(10000, 7500));
}

TEST(MurphyM4Batch, DefaultsOutsideFirstBatchGates) {
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(5199, 5200));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(5200, 5199));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(10001, 10000));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(10000, 10001));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(10000, 5200));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(5200, 6501));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(6218, 3109));
  EXPECT_FALSE(MurphyM4BatchDetection::isFirstBatch(0, 0));
}

TEST(MurphyM4Batch, MedianRejectsOutliers) {
  std::array<uint32_t, MurphyM4BatchDetection::SAMPLE_COUNT> samples = {
      6900, 12000, 6800, 7000, 100, 6950, 6850,
  };
  EXPECT_EQ(MurphyM4BatchDetection::median(samples), 6900U);
}

TEST(MurphyM4Batch, AppliesReferenceTouchCalibration) {
  using freeink::MurphyM4Batch;
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(40, MurphyM4Batch::First, 479), 73, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(330, MurphyM4Batch::First, 479), 303, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(389, MurphyM4Batch::First, 479), 349, 1);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(553, MurphyM4Batch::First, 479), 479);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(514, MurphyM4Batch::Second, 479), 479);
}
