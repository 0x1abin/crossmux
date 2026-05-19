#pragma once

#include <cstdint>
#include <memory>

#include "SloppyEngine.h"
#include "StandbyFace.h"

// "Sloppy clock" — the default Standby face. Renders the current HH:MM using
// a randomized hand-drawn Bezier style. Up/Down shake reshuffles the style.
class SloppyClockFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  void onShake(uint32_t seed) override;
  // Sloppy clock has no day/page concept — both Up and Down forward to the
  // existing shake-reroll behaviour so the gesture keeps doing something.
  void onPagePrev() override;
  void onPageNext() override;
  bool tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override { return StrId::STR_FACE_SLOPPY_CLOCK; }
  uint32_t secondsUntilNextWake() const override;

 private:
  std::unique_ptr<sloppy::Style> style_;
  std::unique_ptr<sloppy::Seeds> seeds_;
  uint32_t startMs_ = 0;   // millis() anchor for the pre-sync fallback display
  int32_t lastMin_ = -1;   // last rendered minute tick, for "did the minute change?" gating

  void regenerate(uint32_t seed);
};
