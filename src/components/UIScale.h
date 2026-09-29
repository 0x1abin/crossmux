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
// Read Pico's 4.7" 1216x684 panel is ~300 PPI, roughly double the other targets'
// density, so the shared 8/10/12 pt UI sizes render about half as large on the glass
// and read as too small. Its UI text therefore asks for 12 and 14 pt through the SAME
// resolver the reader uses, so the selected reading family serves the UI too and the
// two never drift.
//
// The fallback is deliberate and matches the request: when the family is absent, no
// family is selected, or it simply ships no face at that size, the slot keeps the
// built-in face at its ORIGINAL size (UI_10 / UI_12) rather than any enlarged one.
//
// INVARIANT: whichever id this returns is used for the rest of that screen, so it must
// still be registered when the screen draws. An SD face is removed from the renderer's
// font map by SdCardFontManager::unloadAll(), which every releaseLoadedFont() caller
// triggers -- so each release has to be paired with a restore, or a slot that resolved
// while the face was resident goes stale and every draw logs "[ERR] [GFX] Font N not
// found" and renders nothing (the WiFi-page outage). Pairings today:
// BootActivity, EpubReaderActivity (x2), SdFirmwareUpdateActivity, TextSettingsActivity,
// WeReadBrowseActivity, and NetworkStartup::prepare() (which restores when the release
// failed to enlarge the largest internal block). BleInput's StartContext::Explicit
// release is the one path still without a restore.
// / Read Pico resolves 12/14 pt through the reader's own resolver, falling back to the
// built-in UI faces at their original sizes.
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
  spec.bodyFontId = uiResolvePointSize(12, UI_10_FONT_ID);
  spec.titleFontId = uiResolvePointSize(14, UI_12_FONT_ID);
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
