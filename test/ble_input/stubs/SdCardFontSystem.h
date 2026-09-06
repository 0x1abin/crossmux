#pragma once
class GfxRenderer;
struct SdCardFontSystem {
  int unloads = 0;
  void releaseLoadedFont(GfxRenderer&) { ++unloads; }
};
inline SdCardFontSystem sdFontSystem;
