#pragma once

#include <BitmapHelpers.h>

#include <cstdint>

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  struct TwoBitPixel {
    bool draw;
    bool state;
  };

  static constexpr int WIDTH = 32;
  static constexpr int HEIGHT = 24;
  uint8_t* framebuffer = nullptr;
  Orientation orientation = Portrait;
  RenderMode mode = BW;
  mutable int writeTargetRequests = 0;

  int getScreenWidth() const { return portrait() ? HEIGHT : WIDTH; }
  int getScreenHeight() const { return portrait() ? WIDTH : HEIGHT; }
  int getDisplayWidth() const { return WIDTH; }
  int getDisplayHeight() const { return HEIGHT; }
  int getDisplayWidthBytes() const { return WIDTH / 8; }
  uint8_t* getWriteTarget() const {
    ++writeTargetRequests;
    return framebuffer;
  }
  int getWriteOriginY() const { return 0; }
  int getWriteRows() const { return HEIGHT; }
  RenderMode getRenderMode() const { return mode; }
  Orientation getOrientation() const { return orientation; }
  bool grayPlanesAreAbsolute() const { return false; }
  bool imagesDitheredInBw() const { return false; }
  static constexpr TwoBitPixel mapTwoBitPixel(RenderMode mode, uint8_t value, bool absolute) {
    if (mode == BW) return {value < 3, true};
    const auto pixel = grayPlanePixel(value, mode == GRAYSCALE_MSB, absolute);
    return {pixel.write, pixel.black};
  }

 private:
  bool portrait() const { return orientation == Portrait || orientation == PortraitInverted; }
};
