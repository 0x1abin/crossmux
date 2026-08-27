#include <gtest/gtest.h>

#include <array>

#include "MurphyM4Batch.h"

TEST(MurphyM4Batch, ClassifiesRiseTimeAndRejectsGuardBand) {
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(2000), freeink::MurphyM4BatchProbe::Second);
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(4500), freeink::MurphyM4BatchProbe::Second);
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(5200), freeink::MurphyM4BatchProbe::First);
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(10000), freeink::MurphyM4BatchProbe::First);
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(4800), freeink::MurphyM4BatchProbe::Inconclusive);
  EXPECT_EQ(freeink::classifyMurphyM4RiseTime(15000), freeink::MurphyM4BatchProbe::Inconclusive);
}

TEST(MurphyM4Batch, MedianRejectsOutliers) {
  std::array<uint32_t, freeink::MURPHY_M4_BATCH_SAMPLE_COUNT> samples = {
      6900, 12000, 6800, 7000, 100, 6950, 6850,
  };
  EXPECT_EQ(freeink::medianMurphyM4RiseTime(samples), 6900U);
}

TEST(MurphyM4Batch, AppliesReferenceTouchCalibration) {
  using freeink::MurphyM4Batch;
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(40, MurphyM4Batch::First, 479), 73, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(330, MurphyM4Batch::First, 479), 303, 1);
  EXPECT_NEAR(freeink::mapMurphyM4TouchShortAxis(389, MurphyM4Batch::First, 479), 349, 1);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(553, MurphyM4Batch::First, 479), 479);
  EXPECT_EQ(freeink::mapMurphyM4TouchShortAxis(514, MurphyM4Batch::Second, 479), 479);
}
