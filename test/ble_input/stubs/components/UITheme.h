#pragma once
#include <GfxRenderer.h>
struct Rect {
  int x, y, width, height;
};
struct UITheme {
  struct Metrics {
    int topPadding = 0, headerHeight = 20, tabBarHeight = 20, verticalSpacing = 6;
  } metrics;
  mutable const char* lastSubHeader = nullptr;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  const UITheme& getTheme() const { return *this; }
  int getListRowStep(bool) const { return 30; }
  int getListPageItems(int height, bool) const { return height / 30; }
  const Metrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer&, bool, bool) const { return {0, 0, 480, 800}; }
  void drawSubHeader(const GfxRenderer&, Rect, const char* text) const { lastSubHeader = text; }
};
#define GUI UITheme::getInstance().getTheme()
