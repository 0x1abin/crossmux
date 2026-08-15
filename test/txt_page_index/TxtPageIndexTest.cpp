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
  EXPECT_TRUE(isSupportedCacheVersion(LAZY_CACHE_VERSION));
  EXPECT_TRUE(isSupportedCacheVersion(ENCODING_CACHE_VERSION));
  EXPECT_TRUE(isSupportedCacheVersion(CACHE_VERSION));
  EXPECT_FALSE(isSupportedCacheVersion(CACHE_VERSION + 1));
  EXPECT_FALSE(canReuseCacheVersion(LEGACY_CACHE_VERSION, false));
  EXPECT_FALSE(canReuseCacheVersion(LAZY_CACHE_VERSION, false));
  EXPECT_TRUE(canReuseCacheVersion(LEGACY_CACHE_VERSION, true));
  EXPECT_TRUE(canReuseCacheVersion(LAZY_CACHE_VERSION, true));
  EXPECT_TRUE(canReuseCacheVersion(ENCODING_CACHE_VERSION, false));
  EXPECT_TRUE(canReuseCacheVersion(CACHE_VERSION, false));
  EXPECT_FALSE(canReuseCacheVersion(CACHE_VERSION + 1, true));
  EXPECT_TRUE(canReuseParagraphLayout(LEGACY_CACHE_VERSION, true, false));
  EXPECT_TRUE(canReuseParagraphLayout(LAZY_CACHE_VERSION, true, false));
  EXPECT_TRUE(canReuseParagraphLayout(ENCODING_CACHE_VERSION, true, false));
  EXPECT_FALSE(canReuseParagraphLayout(ENCODING_CACHE_VERSION, false, true));
  EXPECT_TRUE(canReuseParagraphLayout(PARAGRAPH_LAYOUT_CACHE_VERSION, false, false));
  EXPECT_TRUE(canReuseParagraphLayout(PARAGRAPH_LAYOUT_CACHE_VERSION, true, true));
  EXPECT_FALSE(canReuseParagraphLayout(PARAGRAPH_LAYOUT_CACHE_VERSION, false, true));
  EXPECT_FALSE(canReuseParagraphLayout(PARAGRAPH_LAYOUT_CACHE_VERSION, true, false));
  EXPECT_EQ(recoveryTargetPage(1000, 32), 62);
  EXPECT_TRUE(shouldCheckpoint(32, false));
  EXPECT_FALSE(shouldCheckpoint(33, false));
  EXPECT_TRUE(shouldCheckpoint(1, true));
}

TEST(TxtPageIndex, MapsSourceOffsetsToKnownPages) {
  using namespace txt_page_index;

  const std::vector<size_t> offsets{0, 100, 240, 400};
  EXPECT_EQ(pageForOffset(offsets, 0), 0U);
  EXPECT_EQ(pageForOffset(offsets, 99), 0U);
  EXPECT_EQ(pageForOffset(offsets, 100), 1U);
  EXPECT_EQ(pageForOffset(offsets, 399), 2U);
  EXPECT_EQ(pageForOffset(offsets, 400), 3U);
  EXPECT_EQ(pageForOffset(offsets, 999), 3U);
  EXPECT_EQ(pageForOffset({}, 20), 0U);
}

TEST(TxtPageIndex, EstimatesDisplayPageForDirectSourceOffset) {
  using namespace txt_page_index;

  EXPECT_EQ(estimatedPageNumber(1000, 0, 10), 1);
  EXPECT_EQ(estimatedPageNumber(1000, 500, 10), 6);
  EXPECT_EQ(estimatedPageNumber(1000, 999, 10), 10);
  EXPECT_EQ(estimatedPageNumber(1000, 1000, 10), 10);
  EXPECT_EQ(estimatedPageNumber(0, 0, 1), 1);
  EXPECT_EQ(estimatedPageNumber(1000, 500, 0), 0);
}
