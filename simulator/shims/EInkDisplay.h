#pragma once

// Shim of open-x4-sdk's EInkDisplay header. Provides only what lib/hal/HalDisplay.h
// references at parse time (dimension constants + member type). The actual rendering
// path is replaced wholesale by simulator/hal/HalDisplay.cpp; no methods
// on this class are ever called in simulator builds.

#include <cstdint>

class EInkDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;

  EInkDisplay() = default;

  // Stubs preserved so HalDisplay.h members + inline helpers still compile.
  void setDisplayX3() {}
  void begin() {}
  void requestResync(int = 0) {}
  void clearScreen(uint8_t = 0xFF) const {}
  void drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) const {}
  void drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool = false) const {}
  void displayBuffer(int = 0, bool = false) {}
  void refreshDisplay(int = 0, bool = false) {}
  void deepSleep() {}
  uint8_t* getFrameBuffer() const { return nullptr; }
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void copyGrayscaleLsbBuffers(const uint8_t*) {}
  void copyGrayscaleMsbBuffers(const uint8_t*) {}
  void cleanupGrayscaleBuffers(const uint8_t*) {}
  void displayGrayBuffer(bool = false) {}
  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  uint16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH / 8; }
};
