#pragma once

#include <cstdint>

#include "I18nKeys.h"
#include "components/themes/BaseTheme.h"  // for ::Rect

class GfxRenderer;

// Base class for a single Standby "face" (e.g. the random sloppy clock, a future
// calendar, etc.). One Face is active at a time; StandbyActivity owns it via
// unique_ptr and switches between faces in response to Left/Right input.
//
// Virtual dispatch is used (rather than std::function or templates) because each
// Face is allocated once per session and called a handful of times per frame;
// the vtable cost is negligible compared to the indirection it removes from
// StandbyActivity.
class StandbyFace {
 public:
  virtual ~StandbyFace() = default;

  // Allocate per-face state. Called once when this face becomes active.
  virtual void onEnter() {}

  // Release per-face state. Called when leaving (face switch or Standby exit).
  virtual void onExit() {}

  // Side Up/Down "shake" gesture. Sloppy clock interprets this as "reroll the
  // style"; other faces may ignore. The seed argument is fresh entropy combined
  // with millis() so the face can re-randomize without seeding its own RNG.
  virtual void onShake(uint32_t /*seed*/) {}

  // Up / Down navigation. StandbyActivity dispatches Up→onPagePrev and
  // Down→onPageNext on the active face. Faces that don't paginate (e.g. the
  // SloppyClock) override these to forward to `onShake` so the gesture still
  // does something useful; faces that do paginate (e.g. the Chinese calendar)
  // override these to change their internal day/page offset.
  virtual void onPagePrev() {}
  virtual void onPageNext() {}

  // Called once per StandbyActivity::loop() tick. Returns true if the screen
  // needs to be redrawn (e.g. the minute boundary moved). Face owns the
  // "did anything change since last render" decision.
  virtual bool tick() = 0;

  // Render the face content within the provided viewport. The activity has
  // already drawn the header / button hints (or cleared the screen in Immersive
  // mode) and called clearScreen() upstream.
  virtual void render(GfxRenderer& renderer, const Rect& viewport) = 0;

  // i18n string id shown in the header when StandbyActivity is in Normal mode
  // and no time-sync is in flight.
  virtual StrId titleId() const = 0;

  // Seconds to sleep before the next timer-driven wake during light-sleep
  // (Sleep mode). Sloppy clock returns "seconds to next minute boundary";
  // a calendar face would return "seconds to next midnight".
  virtual uint32_t secondsUntilNextWake() const = 0;

  // True if this face wants a 4-level grayscale enhancement layered on top
  // of its BW image. When eligible, StandbyActivity calls render() two
  // additional times (LSB then MSB passes) and commits via displayGrayBuffer.
  // Faces that opt in must keep render() idempotent so the repeated
  // invocations produce the same picture.
  //
  // Policy: for passive faces the pass only fires in Immersive (full-screen)
  // mode and when inverseMode is off — see StandbyActivity::render /
  // applyGrayscalePass. Normal-mode renders skip it to keep face/page
  // navigation responsive.
  virtual bool wantsGrayscale() const { return false; }

  // Note on per-orientation availability: StandbyActivity decides whether to
  // include a face in the active rotation via the `FaceEntry::isAvailable
  // (sw, sh)` predicate declared in StandbyActivity.cpp's face table. Faces
  // themselves don't need to know about screen dimensions or orientation.
};
