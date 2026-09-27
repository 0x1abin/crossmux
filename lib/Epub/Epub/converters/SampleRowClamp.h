#pragma once

#include <cstdint>

// Destination -> source index mapping for the block-based image converters.
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
//
// X has the same shape. There the converter's interior fast path assumed its own
// range split had already excluded the boundary; it had not, because the split is
// derived from fineScaleFPX (a truncating divide, rounded down) while sampling
// divides by invScaleFPX (also rounded down), and the two round in opposite
// directions:
//
//   100x100 -> 60x60, block x = 80, validW = 16, split start dstX = 48
//   (48 * 109226) >> 16 == 79  ->  lx0 = 79 - 80 = -1
//
// Every X path therefore samples through sampleColsFor() below instead of
// keeping its own bounds logic, the same way every row goes through
// sampleRowsFor().

// Clamps a source index - row or column - into [0, extent - 1]. extent is
// guaranteed positive by the callers (the draw callbacks return early when the
// block is degenerate).
inline constexpr int clampSampleIndex(const int32_t sourceIndex, const int extent) {
  if (sourceIndex < 0) return 0;
  if (sourceIndex >= extent) return extent - 1;
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
  const int top = (srcFyFP >> 16) - blockY;
  return SampleRows{clampSampleIndex(top, blockH), clampSampleIndex(top + 1, blockH)};
}

// The two source columns a bilinear destination column blends, both clamped.
struct SampleCols {
  int col0;
  int col1;

  friend constexpr bool operator==(const SampleCols&, const SampleCols&) = default;
};

// `dstX` is the destination column within the output image, `invScaleFP` the
// 16.16 source-per-destination step on the X axis, `blockX` the block's first
// source column and `blockW` its number of valid columns. Same arithmetic as the
// rows; every X path in the converter calls this one helper, so the two axes can
// no longer disagree about where a block's samples live.
inline constexpr SampleCols sampleColsFor(const int32_t dstX, const int32_t invScaleFP, const int blockX,
                                          const int blockW) {
  const int32_t srcFxFP = dstX * invScaleFP;
  const int left = (srcFxFP >> 16) - blockX;
  return SampleCols{clampSampleIndex(left, blockW), clampSampleIndex(left + 1, blockW)};
}
