#pragma once

#include <cstdint>

namespace pngHelpers {

constexpr uint8_t blendWithWhite(const uint8_t gray, const uint8_t alpha) {
  return static_cast<uint8_t>((static_cast<uint16_t>(gray) * alpha + 255u * (255u - alpha) + 127u) / 255u);
}

constexpr uint8_t overlayPaletteIndex(const uint8_t grayLevel, const bool opaque) { return opaque ? grayLevel : 4; }

constexpr bool isOverlayPixelOpaque(const uint16_t alpha) { return alpha != 0; }

constexpr bool isSupportedFormat(const uint8_t colorType, const uint8_t bitDepth) {
  switch (colorType) {
    case 0:  // Grayscale
      return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16;
    case 2:  // RGB
    case 4:  // Grayscale + alpha
    case 6:  // RGBA
      return bitDepth == 8 || bitDepth == 16;
    case 3:  // Palette
      return bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8;
    default:
      return false;
  }
}

}  // namespace pngHelpers
