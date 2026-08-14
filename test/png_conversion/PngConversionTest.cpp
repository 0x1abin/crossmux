#include <gtest/gtest.h>

#include "PngHelpers.h"

TEST(PngConversion, CompositesAlphaAgainstWhite) {
  EXPECT_EQ(pngHelpers::blendWithWhite(0, 0), 255);
  EXPECT_EQ(pngHelpers::blendWithWhite(0, 255), 0);
  EXPECT_EQ(pngHelpers::blendWithWhite(255, 128), 255);
  EXPECT_EQ(pngHelpers::blendWithWhite(0, 128), 127);
  EXPECT_EQ(pngHelpers::blendWithWhite(100, 128), 177);
}

TEST(PngConversion, KeepsOpaqueWhiteDistinctFromTransparency) {
  EXPECT_FALSE(pngHelpers::isOverlayPixelOpaque(0));
  EXPECT_TRUE(pngHelpers::isOverlayPixelOpaque(1));
  EXPECT_TRUE(pngHelpers::isOverlayPixelOpaque(128));
  EXPECT_TRUE(pngHelpers::isOverlayPixelOpaque(255));
  EXPECT_TRUE(pngHelpers::isOverlayPixelOpaque(65535));

  EXPECT_EQ(pngHelpers::overlayPaletteIndex(0, true), 0);
  EXPECT_EQ(pngHelpers::overlayPaletteIndex(3, true), 3);
  EXPECT_EQ(pngHelpers::overlayPaletteIndex(0, false), 4);
  EXPECT_EQ(pngHelpers::overlayPaletteIndex(3, false), 4);

  const uint8_t semiTransparentWhite = pngHelpers::blendWithWhite(255, 128);
  EXPECT_EQ(pngHelpers::overlayPaletteIndex(semiTransparentWhite >> 6, true), 3);
}

TEST(PngConversion, AcceptsOnlyLegalColorTypeAndDepthCombinations) {
  for (const uint8_t depth : {1, 2, 4, 8, 16}) EXPECT_TRUE(pngHelpers::isSupportedFormat(0, depth));
  for (const uint8_t depth : {8, 16}) {
    EXPECT_TRUE(pngHelpers::isSupportedFormat(2, depth));
    EXPECT_TRUE(pngHelpers::isSupportedFormat(4, depth));
    EXPECT_TRUE(pngHelpers::isSupportedFormat(6, depth));
  }
  for (const uint8_t depth : {1, 2, 4, 8}) EXPECT_TRUE(pngHelpers::isSupportedFormat(3, depth));

  EXPECT_FALSE(pngHelpers::isSupportedFormat(2, 4));
  EXPECT_FALSE(pngHelpers::isSupportedFormat(3, 16));
  EXPECT_FALSE(pngHelpers::isSupportedFormat(4, 4));
  EXPECT_FALSE(pngHelpers::isSupportedFormat(1, 8));
  EXPECT_FALSE(pngHelpers::isSupportedFormat(7, 8));
}
