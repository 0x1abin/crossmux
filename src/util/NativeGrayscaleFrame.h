#pragma once

#include <GfxRenderer.h>

// A repaint owns one existing native target; abandoned frames never reach the panel.
class NativeGrayscaleFrame {
 public:
  NativeGrayscaleFrame(GfxRenderer& renderer, const bool enabled) : renderer(renderer) {
    owned = enabled && renderer.supportsNativeGrayscaleRefreshCycle() && !renderer.isInverted() &&
            renderer.getGrayscaleLevels() == 16 && renderer.beginGrayscale16();
  }
  ~NativeGrayscaleFrame() {
    if (owned) renderer.cancelGrayscale16();
  }
  bool active() const { return owned && renderer.isGrayscale16Active(); }
  NativeGrayscaleFrame(const NativeGrayscaleFrame&) = delete;
  NativeGrayscaleFrame& operator=(const NativeGrayscaleFrame&) = delete;

 private:
  GfxRenderer& renderer;
  bool owned = false;
};
