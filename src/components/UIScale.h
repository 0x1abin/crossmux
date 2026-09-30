#pragma once
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives its default metric tokens from the body
// font's line height. CrossMux list screens historically use UI_10; titles
// remain UI_12.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec() {
  // Keep stable slots across font unloads; the renderer binds them to the
  // selected family's resident UI sizes and falls back to embedded faces.
  return {UI_10_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID};
}
