#pragma once
#include "CrossPointSettings.h"
#include "UiHighDpiProfile.h"
#include "fontIds.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives its default metric tokens from the body
// font's line height. Only INX retains CrossMux's historical UI_10 body;
// other themes use the upstream UI_12 body. Titles remain UI_12.
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpec(bool upstreamStyle = false) {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId =
      !upstreamStyle && SETTINGS.uiTheme == CrossPointSettings::UI_THEME::INX ? UI_10_FONT_ID : UI_12_FONT_ID;
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = UI_12_FONT_ID;
  if (UiHighDpiProfile::enabled) {
    spec.smallFontId = SMALL_FONT_ID;
    spec.bodyFontId = UI_10_FONT_ID;
    spec.titleFontId = UI_12_FONT_ID;
  }
  return spec;
}
