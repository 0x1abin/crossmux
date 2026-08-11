#pragma once

#include <Adafruit_GFX.h>
#include <EInkDisplay.h>
#include <InputManager.h>

#include "EegoA4Hardware.h"

class PortraitCanvas final : public Adafruit_GFX {
 public:
  enum class DrawingRegion : uint8_t {
    UiContentRect,
    FullPanel,
  };

  explicit PortraitCanvas(EInkDisplay& display)
      : Adafruit_GFX(eego::LOGICAL_WIDTH, eego::LOGICAL_HEIGHT), display_(display) {}

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    if (drawingRegion_ == DrawingRegion::UiContentRect &&
        !eego::isInsideUiContentRect(x, y)) {
      return;
    }
    uint8_t* framebuffer = display_.getFrameBuffer();
    if (!framebuffer) return;

    // Logical portrait -> panel-native landscape, 90 degrees clockwise.
    const uint16_t nativeX = static_cast<uint16_t>(y);
    const uint16_t nativeY = static_cast<uint16_t>(eego::PANEL_HEIGHT - 1 - x);
    const uint32_t offset =
        static_cast<uint32_t>(nativeY) * eego::PANEL_ROW_BYTES + nativeX / 8;
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (nativeX & 7U));
    if (color == 0) {
      framebuffer[offset] &= static_cast<uint8_t>(~mask);
    } else {
      framebuffer[offset] |= mask;
    }
  }

  void clear(uint16_t color = 1) {
    // A page background is allowed to cover the physical aperture. Foreground
    // drawing after clear() remains clipped to the selected DrawingRegion.
    display_.clearScreen(color == 0 ? 0x00 : 0xff);
  }

  // UiContentRect is the default zero-radius 496x712 foreground contract.
  // Restore it immediately after any explicit full-panel background, edge,
  // touch-range, or waveform test.
  void useUiContentRect() {
    drawingRegion_ = DrawingRegion::UiContentRect;
  }

  void useFullPanel() { drawingRegion_ = DrawingRegion::FullPanel; }
  DrawingRegion drawingRegion() const { return drawingRegion_; }

  static void touchToLogical(const InputManager::TouchPoint& point, int16_t& x, int16_t& y) {
    x = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - point.y);
    y = static_cast<int16_t>(point.x);
  }

 private:
  EInkDisplay& display_;
  DrawingRegion drawingRegion_ = DrawingRegion::UiContentRect;
};
