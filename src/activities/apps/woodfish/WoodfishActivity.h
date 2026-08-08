#pragma once

#include <SloppyDigits.h>

#include <cstdint>

#include "../../Activity.h"

struct Rect;

class WoodfishActivity final : public Activity {
 public:
  WoodfishActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Woodfish", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct HitBounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int pointX, int pointY) const {
      return pointX >= x && pointX < x + width && pointY >= y && pointY < y + height;
    }
  };

  void knock(uint32_t now);
  bool persist();
  void drawWoodfish(const Rect& bounds);
  void drawTotal(const Rect& bounds) const;

  uint32_t total_ = 0;
  uint32_t saveIdleSinceMs_ = 0;
  uint32_t feedbackStartedMs_ = 0;
  bool dirty_ = false;
  bool feedbackActive_ = false;
  bool feedbackShowsIncrement_ = false;
  HitBounds woodfishHitBounds_;
  sloppy::Seeds digitSeeds_{};
};
