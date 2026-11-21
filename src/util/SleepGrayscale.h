#pragma once

// Shared sleep-screen grayscale predicates.
//
// The sleep cover bitmap has two independent sources that must agree on dither
// thresholds:
//   - the last book's cover            (SleepActivity)
//   - a user image via "set as cover"  (ImageViewerActivity -> /sleep.bmp)
// When they disagree one path silently renders against the skewed legacy ramp
// (thresholds 30/55/150, error targets 15/35/90/210) while the other uses the
// native 0/85/170/255 ramp, so the two look noticeably different on the same
// panel. These predicates live here so both paths make the same decision.

#include <GfxRenderer.h>
#include <HalDisplay.h>

inline HalDisplay::GrayscaleMode sleepGrayscaleMode(const GfxRenderer& renderer) {
  return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
             ? HalDisplay::GrayscaleMode::Direct
             : HalDisplay::GrayscaleMode::Absolute;
}

// Original (0/85/170/255) dither thresholds match the native 4-level ramp of
// the X4/X4 Pro 800x480 glass, whether it carries an SSD1677, UC8179 or UC8279
// controller. X3 (792x528) and other panels retain the legacy tuning.
inline bool useOriginalGrayscaleThresholds() {
  // Physical panel geometry — logical size rotates with orientation, so accept
  // the 800x480 glass whichever way round the panel registers its native scan.
  const bool is800x480 = (display.getDisplayWidth() == 800 && display.getDisplayHeight() == 480) ||
                         (display.getDisplayWidth() == 480 && display.getDisplayHeight() == 800);
  switch (display.getController()) {
    case HalDisplay::Controller::SSD1677:
    case HalDisplay::Controller::UC8279:
    case HalDisplay::Controller::UC8179:
      return is800x480;
    default:
      return false;
  }
}
