#pragma once

#include <cstdint>

// Destination row -> source row mapping for the block-based image converters.
//
// Extracted from JpegToFramebufferConverter so the block boundary is testable
// without a renderer, cache or decoder stub. The bug this exists to prevent:
// computing `ly1 = ly0 + 1` *before* clamping lets both rows go negative at the
// first destination row of a shifted block, and `row1` then points before the
// decoded block:
//
//   100x100 -> 60x60, block y = 16, dstYStart = 9
//   fineScaleFPY = 39321, invScaleFPY = 109226   (both truncated, as production computes them)
//   srcFyFP = 9 * 109226 = 983034  ->  >> 16 == 14  ->  ly0 = 14 - 16 = -2, ly1 = -1
//
// Clamping ly0 alone fixes row0 and leaves row1 at -1.

// Clamps a source row index into [0, blockH - 1]. blockH is guaranteed positive
// by the callers (the draw callbacks return early when the block is degenerate).
inline constexpr int clampSampleRow(const int32_t sourceIndex, const int blockH) {
  if (sourceIndex < 0) return 0;
  if (sourceIndex >= blockH) return blockH - 1;
  return static_cast<int>(sourceIndex);
}

// The two source rows a bilinear destination row blends, both clamped.
struct SampleRows {
  int row0;
  int row1;

  friend constexpr bool operator==(const SampleRows&, const SampleRows&) = default;
};

// `dstY` is the destination row within the output image, `invScaleFP` the 16.16
// source-per-destination step on this axis, `blockY` the block's first source row
// and `blockH` its height. Mirrors the converter's fixed-point arithmetic exactly
// (16.16, truncating shift) so callers can keep using the same FP values.
inline constexpr SampleRows sampleRowsFor(const int32_t dstY, const int32_t invScaleFP, const int blockY,
                                          const int blockH) {
  const int32_t srcFyFP = dstY * invScaleFP;
  const int32_t top = (srcFyFP >> 16) - blockY;
  return SampleRows{clampSampleRow(top, blockH), clampSampleRow(top + 1, blockH)};
}
