#pragma once

#include <cstdint>
#include <memory>

#include "ChineseAlmanac.h"
#include "SloppyEngine.h"
#include "StandbyFace.h"

// "Chinese Traditional Calendar" — paper-黄历 standby face.
//
// Portrait-only (StandbyActivity hides it in landscape) and CN-build-only
// (`ENABLE_CHINESE_VERSION` gates registration; non-CN bitmap fonts have no
// CJK glyphs). All date values come from `computeAlmanac()` (real lunar /
// ganzhi / solar-term routine). The hero day digit reuses SloppyEngine
// (zero-jitter Geometric style).
//
// Up/Down navigates `dayOffset_` (relative to today); Left/Right (handled by
// StandbyActivity) switches between Faces.
class ChineseCalendarFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  bool tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override;
  bool requiresPortrait() const override { return true; }

  // Up → previous day; Down → next day. Clamped to 1900-01-01 / 2100-12-31.
  void onPagePrev() override;
  void onPageNext() override;

 private:
  std::unique_ptr<sloppy::Style> heroStyle_;
  std::unique_ptr<sloppy::Seeds> heroSeeds_;

  // Day navigation: relative to "today" (per `time(nullptr)` in UTC+8).
  int32_t dayOffset_ = 0;
  AlmanacDay cachedDay_{};
  bool cacheValid_ = false;
  // Cached "what day was today the last time we refreshed?" so `tick()` can
  // detect midnight crossover when the user is parked on offset 0.
  int32_t cachedBaseDayKey_ = -1;

  // Recompute `cachedDay_` from current local time + dayOffset_. Returns
  // false if the resulting date is outside 1900-2100 or system time isn't
  // available; callers should restore `dayOffset_` to a known-good value.
  bool refreshCachedDay();
};
