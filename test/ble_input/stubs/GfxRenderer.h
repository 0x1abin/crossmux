#pragma once
#include "FontCacheManager.h"
class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation = Portrait;
  FontCacheManager cache;
  FontCacheManager* getFontCacheManager() { return &cache; }
  Orientation getOrientation() const { return orientation; }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void tapToLogical(float nx, float ny, int& x, int& y) const {
    x = static_cast<int>(nx * getScreenWidth());
    y = static_cast<int>(ny * getScreenHeight());
  }
};
