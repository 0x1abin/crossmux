#pragma once

#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <Utf8.h>

#include <deque>
#include <string>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}

class GfxRenderer {
 public:
  FontCacheManager fontCache;
  FontCacheManager* getFontCacheManager() { return &fontCache; }
  enum class TextMeasureMode { Layout, Rendered };
  // Fixture metrics: every glyph is 8 px wide, a space is 4 px, kerning is zero.
  static int trackingBetween(uint32_t left, uint32_t right, int8_t tracking) {
    return left == 0 || left == ' ' || right == ' ' ? 0 : tracking;
  }
  bool isFontCacheScanning() const { return false; }
  void drawLine(int, int, int, int, int, bool) const {}
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style,
                BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO, int8_t = 0) const {}
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style,
                   BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO) const {
    return getTextAdvanceX(font, text, style);
  }
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float = 1.0f) const { return 16; }
  int getFontAscenderSize(int) const { return 12; }
  int getSpaceWidth(int, EpdFontFamily::Style) const { return 4; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style, int8_t tracking = 0,
                      BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO,
                      TextMeasureMode = TextMeasureMode::Layout) const {
    int width = 0;
    // Preserve the historical byte-based fixture used by the cache digest.
    // The upstream spacing suite has a separate glyph-based renderer seam.
    while (*text++) width += 8;
    return width;
  }
  int getKerning(int, uint32_t left, uint32_t right, EpdFontFamily::Style, int8_t tracking = 0) const {
    return trackingBetween(left, right, tracking);
  }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 4; }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const char* const*, const size_t*, size_t, bool, bool, uint8_t) const {}
};
