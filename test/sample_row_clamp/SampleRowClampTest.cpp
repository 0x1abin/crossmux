// Host regression tests for the block index mapping used by the JPEG framebuffer
// converter's bilinear path.
//
// Review finding on PR #308 (rows): `ly1 = ly0 + 1` was computed before
// clamping, and only ly0's lower bound was clamped, so the first destination row
// of a shifted block could leave ly1 negative and form `row1 = pixels + ly1 *
// stride` before the decoded block.
//
// Review finding on PR #308 (columns): the interior fast path assumed its own
// range split had already excluded the block boundary. It had not, because the
// split is derived from fineScaleFPX while the samples come from invScaleFPX,
// and both divide with truncation. For 100x100 -> 60x60 the block at x = 80
// starts its interior at dstX = 48, whose first sample is
// (48 * 109226) >> 16 - 80 = -1.
//
// These tests drive the same helpers the converter now calls on both axes, with
// the same 16.16 arithmetic, and sweep the block boundaries under non-integer
// ratios.

#include <SampleRowClamp.h>

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace {

constexpr int32_t kOne = 1 << 16;

// Mirrors JpegToFramebufferConverter's scale derivation (truncating integer
// division), so the fixtures reproduce the production fixed-point values.
int32_t fineScaleFP(const int dstSize, const int srcSize) {
  return static_cast<int32_t>(static_cast<int64_t>(dstSize) * kOne / srcSize);
}

int32_t invScaleFP(const int srcSize, const int dstSize) {
  return static_cast<int32_t>(static_cast<int64_t>(srcSize) * kOne / dstSize);
}

// Mirrors the callback's destination range start for one source block, on either
// axis. `blockOffset` is the block's first source row or column.
int dstStart(const int blockOffset, const int32_t fineScale) {
  return static_cast<int>((static_cast<int64_t>(blockOffset) * fineScale) >> 16);
}

// Mirrors the callback's interior split start on X: the first destination column
// whose source column is meant to be inside the block.
int interiorStartX(const int blockX, const int32_t fineScale) {
  return static_cast<int>((static_cast<int64_t>(blockX) * fineScale + 0xFFFF) >> 16);
}

}  // namespace

// The exact case from the row review: 100x100 -> 60x60, the 16x16 block at y = 16.
TEST(SampleRowClamp, ClampsBothRowsForTheReviewReproduction) {
  constexpr int srcSize = 100;
  constexpr int dstSize = 60;
  constexpr int blockH = 16;
  constexpr int blockY = 16;

  const int32_t fine = fineScaleFP(dstSize, srcSize);
  const int32_t inv = invScaleFP(srcSize, dstSize);
  ASSERT_EQ(39321, fine);
  ASSERT_EQ(109226, inv);

  const int start = dstStart(blockY, fine);
  ASSERT_EQ(9, start);

  // Documenting the defect shape: without clamping, the first destination row of
  // this block maps two rows above the block start.
  EXPECT_EQ(-2, (9 * inv >> 16) - blockY);

  const SampleRows rows = sampleRowsFor(start, inv, blockY, blockH);
  EXPECT_EQ(0, rows.row0);
  EXPECT_EQ(0, rows.row1);
  EXPECT_GE(rows.row1, 0);
  EXPECT_LT(rows.row0, blockH);
  EXPECT_LT(rows.row1, blockH);
}

// The exact case from the column review: the same 100x100 -> 60x60 geometry, the
// 16-wide block at x = 80. The interior split starts at dstX = 48 and its first
// sample would be -1 without the clamp.
TEST(SampleRowClamp, ClampsBothColumnsForTheInteriorLoopReproduction) {
  constexpr int srcSize = 100;
  constexpr int dstSize = 60;
  constexpr int blockW = 16;
  constexpr int blockX = 80;

  const int32_t fine = fineScaleFP(dstSize, srcSize);
  const int32_t inv = invScaleFP(srcSize, dstSize);
  ASSERT_EQ(39321, fine);
  ASSERT_EQ(109226, inv);

  const int splitStart = interiorStartX(blockX, fine);
  ASSERT_EQ(48, splitStart);

  // Documenting the defect shape: the split's own first sample falls one column
  // before the block, which is what the interior loop used to index.
  EXPECT_EQ(-1, (splitStart * inv >> 16) - blockX);

  const SampleCols cols = sampleColsFor(splitStart, inv, blockX, blockW);
  EXPECT_EQ(0, cols.col0);
  EXPECT_EQ(0, cols.col1);
  EXPECT_GE(cols.col0, 0);
  EXPECT_LT(cols.col0, blockW);
  EXPECT_LT(cols.col1, blockW);
}

TEST(SampleRowClamp, ClampSampleIndexHoldsTheExtentBounds) {
  EXPECT_EQ(0, clampSampleIndex(-1000, 16));
  EXPECT_EQ(0, clampSampleIndex(-1, 16));
  EXPECT_EQ(0, clampSampleIndex(0, 16));
  EXPECT_EQ(15, clampSampleIndex(15, 16));
  EXPECT_EQ(15, clampSampleIndex(16, 16));
  EXPECT_EQ(15, clampSampleIndex(1000, 16));
  // Degenerate blocks clamp to their only index; callers reject extent <= 0 first.
  EXPECT_EQ(0, clampSampleIndex(-5, 1));
  EXPECT_EQ(0, clampSampleIndex(5, 1));
}

// Sweeps every destination row of every block for a spread of ratios, including
// non-integer shrink factors, tiny images and block edges.
TEST(SampleRowClamp, BothRowsStayInsideTheBlockAcrossRatiosAndOffsets) {
  const std::vector<int> srcSizes = {1, 2, 3, 15, 16, 17, 100, 101, 640, 1000};
  const std::vector<int> dstSizes = {1, 2, 3, 7, 15, 16, 17, 33, 60, 99, 100, 333, 640};
  const std::vector<int> blockHeights = {1, 2, 3, 8, 15, 16};

  for (const int srcSize : srcSizes) {
    for (const int dstSize : dstSizes) {
      const int32_t fine = fineScaleFP(dstSize, srcSize);
      const int32_t inv = invScaleFP(srcSize, dstSize);
      if (fine <= 0 || inv <= 0) continue;

      for (const int blockH : blockHeights) {
        for (int blockY = 0; blockY < srcSize; blockY += blockH) {
          const int start = dstStart(blockY, fine);
          const int srcEnd = blockY + blockH;
          const int end = srcEnd >= srcSize ? dstSize : dstStart(srcEnd, fine);

          for (int dstY = start; dstY < end; ++dstY) {
            const SampleRows rows = sampleRowsFor(dstY, inv, blockY, blockH);
            // The invariant the crash came from: a pointer formed from either row
            // must land inside the decoded block.
            ASSERT_GE(rows.row0, 0) << "src " << srcSize << " dst " << dstSize << " blockH " << blockH << " blockY "
                                    << blockY << " dstY " << dstY;
            ASSERT_GE(rows.row1, 0) << "src " << srcSize << " dst " << dstSize << " blockH " << blockH << " blockY "
                                    << blockY << " dstY " << dstY;
            ASSERT_LT(rows.row0, blockH);
            ASSERT_LT(rows.row1, blockH);
            // The pair is adjacent: a blend samples the row it is on and the next.
            EXPECT_LE(rows.row1 - rows.row0, 1);
            EXPECT_GE(rows.row1 - rows.row0, 0);
          }
        }
      }
    }
  }
}

// The same sweep on X, covering every destination column of every block - the
// interior range included, which is the path the second review finding reported.
TEST(SampleRowClamp, BothColumnsStayInsideTheBlockAcrossRatiosAndOffsets) {
  const std::vector<int> srcSizes = {1, 2, 3, 15, 16, 17, 100, 101, 640, 1000};
  const std::vector<int> dstSizes = {1, 2, 3, 7, 15, 16, 17, 33, 60, 99, 100, 333, 640};
  const std::vector<int> blockWidths = {1, 2, 3, 8, 15, 16};

  for (const int srcSize : srcSizes) {
    for (const int dstSize : dstSizes) {
      const int32_t fine = fineScaleFP(dstSize, srcSize);
      const int32_t inv = invScaleFP(srcSize, dstSize);
      if (fine <= 0 || inv <= 0) continue;

      for (const int blockW : blockWidths) {
        for (int blockX = 0; blockX < srcSize; blockX += blockW) {
          const int start = dstStart(blockX, fine);
          const int srcEnd = blockX + blockW;
          const int end = srcEnd >= srcSize ? dstSize : dstStart(srcEnd, fine);

          for (int dstX = start; dstX < end; ++dstX) {
            const SampleCols cols = sampleColsFor(dstX, inv, blockX, blockW);
            // A column pointer or index built from either sample has to land
            // inside the block's valid columns.
            ASSERT_GE(cols.col0, 0) << "src " << srcSize << " dst " << dstSize << " blockW " << blockW << " blockX "
                                    << blockX << " dstX " << dstX;
            ASSERT_GE(cols.col1, 0) << "src " << srcSize << " dst " << dstSize << " blockW " << blockW << " blockX "
                                    << blockX << " dstX " << dstX;
            ASSERT_LT(cols.col0, blockW);
            ASSERT_LT(cols.col1, blockW);
            EXPECT_LE(cols.col1 - cols.col0, 1);
            EXPECT_GE(cols.col1 - cols.col0, 0);
          }
        }
      }
    }
  }
}

// A 1:1 mapping samples the row it lands on plus the next one, and both clamp
// back into the block at its last row.
TEST(SampleRowClamp, IdentityMappingNeverLeavesTheBlock) {
  EXPECT_EQ(SampleRows({0, 1}), sampleRowsFor(0, kOne, 0, 16));
  EXPECT_EQ(SampleRows({7, 8}), sampleRowsFor(7, kOne, 0, 16));
  EXPECT_EQ(SampleRows({14, 15}), sampleRowsFor(14, kOne, 0, 16));
  // Last row of the block, and one row past it: row1 clamps onto row 15.
  EXPECT_EQ(SampleRows({15, 15}), sampleRowsFor(15, kOne, 0, 16));
  EXPECT_EQ(SampleRows({15, 15}), sampleRowsFor(16, kOne, 0, 16));
}

TEST(SampleRowClamp, IdentityColumnsNeverLeaveTheBlock) {
  EXPECT_EQ(SampleCols({0, 1}), sampleColsFor(0, kOne, 0, 16));
  EXPECT_EQ(SampleCols({7, 8}), sampleColsFor(7, kOne, 0, 16));
  EXPECT_EQ(SampleCols({14, 15}), sampleColsFor(14, kOne, 0, 16));
  EXPECT_EQ(SampleCols({15, 15}), sampleColsFor(15, kOne, 0, 16));
  EXPECT_EQ(SampleCols({15, 15}), sampleColsFor(16, kOne, 0, 16));
}

// Upscaling repeats rows (4 source rows -> 16 destination rows, step 0.25); the
// pair must stay inside the block at both ends.
TEST(SampleRowClamp, UpscalingStaysInsideTheBlockAtBothEnds) {
  const int32_t inv = invScaleFP(4, 16);
  EXPECT_EQ(SampleRows({0, 1}), sampleRowsFor(0, inv, 0, 4));
  EXPECT_EQ(SampleRows({1, 2}), sampleRowsFor(4, inv, 0, 4));
  EXPECT_EQ(SampleRows({2, 3}), sampleRowsFor(8, inv, 0, 4));
  EXPECT_EQ(SampleRows({3, 3}), sampleRowsFor(12, inv, 0, 4));
  EXPECT_EQ(SampleRows({3, 3}), sampleRowsFor(15, inv, 0, 4));
}

TEST(SampleRowClamp, UpscalingColumnsStayInsideTheBlockAtBothEnds) {
  const int32_t inv = invScaleFP(4, 16);
  EXPECT_EQ(SampleCols({0, 1}), sampleColsFor(0, inv, 0, 4));
  EXPECT_EQ(SampleCols({1, 2}), sampleColsFor(4, inv, 0, 4));
  EXPECT_EQ(SampleCols({2, 3}), sampleColsFor(8, inv, 0, 4));
  EXPECT_EQ(SampleCols({3, 3}), sampleColsFor(12, inv, 0, 4));
  EXPECT_EQ(SampleCols({3, 3}), sampleColsFor(15, inv, 0, 4));
}

// A block that does not start at source row 0 clamps against its own bounds: the
// block offset is subtracted before clamping, not after.
TEST(SampleRowClamp, BlockOffsetClampsAgainstTheBlockNotTheImage) {
  EXPECT_EQ(SampleRows({0, 1}), sampleRowsFor(16, kOne, 16, 16));
  EXPECT_EQ(SampleRows({1, 2}), sampleRowsFor(17, kOne, 16, 16));
  EXPECT_EQ(SampleRows({15, 15}), sampleRowsFor(31, kOne, 16, 16));
  EXPECT_EQ(SampleRows({15, 15}), sampleRowsFor(32, kOne, 16, 16));
}

TEST(SampleRowClamp, BlockOffsetClampsColumnsAgainstTheBlockNotTheImage) {
  EXPECT_EQ(SampleCols({0, 1}), sampleColsFor(16, kOne, 16, 16));
  EXPECT_EQ(SampleCols({1, 2}), sampleColsFor(17, kOne, 16, 16));
  EXPECT_EQ(SampleCols({15, 15}), sampleColsFor(31, kOne, 16, 16));
  EXPECT_EQ(SampleCols({15, 15}), sampleColsFor(32, kOne, 16, 16));
}

// A one-row block: every destination row collapses onto that single row, which is
// the tightest form of the boundary the fix is about.
TEST(SampleRowClamp, SingleRowBlockCollapsesBothSamplesOntoIt) {
  const int32_t inv = invScaleFP(1, 3);
  EXPECT_EQ(SampleRows({0, 0}), sampleRowsFor(0, inv, 0, 1));
  EXPECT_EQ(SampleRows({0, 0}), sampleRowsFor(1, inv, 0, 1));
  EXPECT_EQ(SampleRows({0, 0}), sampleRowsFor(2, inv, 0, 1));
}

// The X counterpart, which is also the shape a partially decoded block has when
// JPEGDEC reports iWidthUsed < iWidth.
TEST(SampleRowClamp, SingleColumnBlockCollapsesBothSamplesOntoIt) {
  const int32_t inv = invScaleFP(1, 3);
  EXPECT_EQ(SampleCols({0, 0}), sampleColsFor(0, inv, 0, 1));
  EXPECT_EQ(SampleCols({0, 0}), sampleColsFor(1, inv, 0, 1));
  EXPECT_EQ(SampleCols({0, 0}), sampleColsFor(2, inv, 0, 1));
}
