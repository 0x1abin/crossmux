#include <gtest/gtest.h>

#include "DirectPixelWriter.h"
#include "GfxRenderer.h"

namespace {

bool mappedBit(const GfxRenderer::TwoBitPixel pixel) { return pixel.draw && !pixel.state; }

bool directBit(const GfxRenderer::RenderMode mode, const uint8_t value, const bool absolute) {
  uint8_t framebuffer = 0;
  DirectPixelWriter writer{};
  writer.fb = &framebuffer;
  writer.mode = mode;
  writer.absolute = absolute;
  writer.displayWidthBytes = 1;
  writer.originY = 0;
  writer.clipRows = 1;
  writer.phyXBase = 0;
  writer.phyYBase = 0;
  writer.phyXStepX = 1;
  writer.phyYStepX = 0;
  writer.phyXStepY = 0;
  writer.phyYStepY = 1;
  writer.beginRow(0);
  writer.writePixel(0, value);
  return (framebuffer & 0x80) != 0;
}

}  // namespace

TEST(GfxTwoBitMapping, OverlayAndAbsoluteDirectGlyphAndBitmapAgreeWithPlaneEncoding) {
  for (const bool absolute : {false, true}) {
    for (const auto mode : {GfxRenderer::GRAYSCALE_MSB, GfxRenderer::GRAYSCALE_LSB}) {
      for (uint8_t value = 0; value < 4; ++value) {
        const bool direct = directBit(mode, value, absolute);
        const bool glyph = mappedBit(GfxRenderer::mapTwoBitGlyphCoverage(mode, 3 - value, absolute));
        const bool bitmap = mappedBit(GfxRenderer::mapTwoBitPixel(mode, value, absolute));
        const bool expected = absolute ? ((value >> (mode == GfxRenderer::GRAYSCALE_MSB ? 1 : 0)) & 1)
                                       : (mode == GfxRenderer::GRAYSCALE_MSB ? value == 1 || value == 2 : value == 1);
        EXPECT_EQ(direct, expected) << "mode=" << mode << " value=" << static_cast<int>(value);
        EXPECT_EQ(glyph, expected);
        EXPECT_EQ(bitmap, expected);
      }
    }
  }
}

TEST(GfxTwoBitMapping, BwKeepsLogicalPixelState) {
  for (uint8_t value = 0; value < 4; ++value) {
    const auto pixel = GfxRenderer::mapTwoBitPixel(GfxRenderer::BW, value);
    EXPECT_EQ(pixel.draw, value < 3);
    EXPECT_TRUE(pixel.state);
  }
}
