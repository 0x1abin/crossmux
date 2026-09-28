#pragma once
#include <CrossPointSettings.h>

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

#if FREEINK_DEVICE_READPICO
// Read Pico's UI text asks for 16 and 18 pt through the SAME resolver the reader
// uses, so an installed SD-card font family serves both and the two never drift.
// The resolver is installed by SdCardFontSystem and returns 0 when the family has
// no face at that size; the built-in UI fonts are then the fallback, exactly as
// getReaderFontId() falls back to the built-in reader faces.
//
// This is why changing BUILTIN_READER_POINT_SIZES alone had no effect on the
// system font: UIScaleSpec used to hardcode the UI_10/UI_12 Ubuntu faces and never
// consulted the resolver at all.
// / Read Pico's UI text resolves 16/18 pt through the reader's own resolver.
inline int uiResolvePointSize(const uint8_t pointSize, const int fallbackFontId) {
  if (SETTINGS.sdFontFamilyName[0] != '\0' && SETTINGS.sdFontIdResolver != nullptr) {
    const int id = SETTINGS.sdFontIdResolver(SETTINGS.sdFontResolverCtx, SETTINGS.sdFontFamilyName, pointSize);
    if (id != 0) return id;
  }
  return fallbackFontId;
}
#endif

inline UIScaleSpec uiScaleSpec() {
  UIScaleSpec spec{};
#if FREEINK_DEVICE_READPICO
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = uiResolvePointSize(16, UI_10_FONT_ID);
  spec.titleFontId = uiResolvePointSize(18, UI_12_FONT_ID);
#else
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = UI_10_FONT_ID;
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = UI_12_FONT_ID;
#endif
  return spec;
}
