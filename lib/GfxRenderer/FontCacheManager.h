#pragma once

#include <EpdFontFamily.h>

#include <cstddef>
#include <cstdint>
#include <map>

class FontDecompressor;
class SdCardFont;

class FontCacheManager {
 public:
  FontCacheManager(const std::map<int, EpdFontFamily>& fontMap, const std::map<int, SdCardFont*>& sdCardFonts);

  void setFontDecompressor(FontDecompressor* d);

  void clearCache();
  void prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F);
  void logStats(const char* label = "render");
  void resetStats();

  // Scan-mode API: called by GfxRenderer::drawText() during scan pass
  bool isScanning() const;
  void recordText(const char* text, int fontId, EpdFontFamily::Style style);

  // The FontDecompressor pointer, needed by GfxRenderer::getGlyphBitmap()
  FontDecompressor* getDecompressor() const { return fontDecompressor_; }

  // RAII scope for two-pass prewarm pattern
  class PrewarmScope {
   public:
    explicit PrewarmScope(FontCacheManager& manager);
    ~PrewarmScope();
    void endScanAndPrewarm();
    PrewarmScope(PrewarmScope&& other) noexcept;
    PrewarmScope& operator=(PrewarmScope&&) = delete;
    PrewarmScope(const PrewarmScope&) = delete;
    PrewarmScope& operator=(const PrewarmScope&) = delete;

   private:
    FontCacheManager* manager_;
    bool active_ = true;
  };
  PrewarmScope createPrewarmScope();

 private:
  const std::map<int, EpdFontFamily>& fontMap_;
  const std::map<int, SdCardFont*>& sdCardFonts_;
  FontDecompressor* fontDecompressor_ = nullptr;

  enum class ScanMode : uint8_t { None, Scanning };
  ScanMode scanMode_ = ScanMode::None;
  // Per-style scan buffers (index = base Style: 0=Regular, 1=Bold, 2=Italic,
  // 3=Bold-Italic). Each page render prewarms every style ONLY against the text
  // actually drawn in that style, not the whole page. Prewarming a secondary style
  // over the entire page produced a full-page-sized FontDecompressor page-slot
  // buffer per style; with the BLE stack resident the later styles' malloc failed,
  // dropping bold/italic onto the hot-group fallback (which also OOM'd) so those
  // glyphs rendered blank. Bucketing by style keeps each secondary buffer as small
  // as the bold/italic text present, so every style reliably gets its slot.
  static constexpr size_t SCAN_TEXT_CAPACITY = 2048;
  static constexpr uint8_t STYLE_COUNT = 4;
  char scanText_[STYLE_COUNT][SCAN_TEXT_CAPACITY] = {};
  size_t scanTextLen_[STYLE_COUNT] = {};
  int scanFontId_ = -1;
};
