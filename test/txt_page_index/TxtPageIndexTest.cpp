#include <gtest/gtest.h>

#include <vector>

#include "TxtPageIndex.h"

TEST(TxtPageIndex, AdvancesLazilyAndFinishesWithExactPageCount) {
  using namespace txt_page_index;

  std::vector<size_t> offsets{0};
  EXPECT_EQ(recordNextOffset(offsets, 1000, 100), AdvanceResult::PageAdded);
  EXPECT_EQ(estimateTotalPages(1000, offsets, false), 10);

  for (size_t offset = 200; offset < 1000; offset += 100) {
    EXPECT_EQ(recordNextOffset(offsets, 1000, offset), AdvanceResult::PageAdded);
  }
  EXPECT_EQ(recordNextOffset(offsets, 1000, 1000), AdvanceResult::Completed);
  EXPECT_EQ(estimateTotalPages(1000, offsets, true), 10);
  EXPECT_EQ(progressPercent(1000, 1000), 100);
}

TEST(TxtPageIndex, BoundsRecoveryAndAcceptsTheCompleteV4Cache) {
  using namespace txt_page_index;

  EXPECT_TRUE(isSupportedCacheVersion(LEGACY_CACHE_VERSION));
  EXPECT_TRUE(isSupportedCacheVersion(CACHE_VERSION));
  EXPECT_FALSE(isSupportedCacheVersion(CACHE_VERSION + 1));
  EXPECT_EQ(recoveryTargetPage(1000, 32), 62);
  EXPECT_TRUE(shouldCheckpoint(32, false));
  EXPECT_FALSE(shouldCheckpoint(33, false));
  EXPECT_TRUE(shouldCheckpoint(1, true));
}
