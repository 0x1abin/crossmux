#pragma once

#include <Arduino.h>
#include <SDCardManager.h>
#include <SdFat.h>

#include "PortraitCanvas.h"

// Minimal CPFONT v4 reader used by the standalone hardware template.
//
// It intentionally implements only the regular-style, left-to-right drawing
// path needed by the diagnostic pages. The production CrossPoint renderer adds
// caching, fallback families, kerning, ligatures, BiDi and grayscale planes.
class CpFontRenderer {
 public:
  struct FontInfo {
    uint32_t fileBytes = 0;
    uint32_t intervalCount = 0;
    uint32_t glyphCount = 0;
    uint8_t advanceY = 0;
    int16_t ascender = 0;
    int16_t descender = 0;
    bool is2Bit = false;
    uint8_t styleId = 0;
  };

  CpFontRenderer(SDCardManager& sd, PortraitCanvas& canvas);
  ~CpFontRenderer();

  CpFontRenderer(const CpFontRenderer&) = delete;
  CpFontRenderer& operator=(const CpFontRenderer&) = delete;

  bool load(const char* path);
  void unload();
  bool loaded() const { return loaded_; }
  const FontInfo& info() const { return info_; }
  const String& error() const { return error_; }

  bool hasCodepoint(uint32_t codepoint) const;
  uint16_t countMissing(const char* utf8Text) const;

  // Draw at a top-left coordinate. Returns the final cursor X and optionally
  // reports missing glyphs and storage/render errors.
  int16_t drawText(int16_t x, int16_t top, const char* utf8Text,
                   uint16_t* missing = nullptr, uint16_t* ioErrors = nullptr);

 private:
  struct Interval {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
  };

  struct Glyph {
    uint8_t width = 0;
    uint8_t height = 0;
    uint16_t advanceX = 0;
    int16_t left = 0;
    int16_t top = 0;
    uint16_t dataLength = 0;
    uint32_t dataOffset = 0;
  };

  SDCardManager& sd_;
  PortraitCanvas& canvas_;
  FsFile file_;
  FontInfo info_{};
  Interval* intervals_ = nullptr;
  uint8_t* bitmap_ = nullptr;
  size_t bitmapCapacity_ = 0;
  uint32_t glyphsFileOffset_ = 0;
  uint32_t bitmapFileOffset_ = 0;
  bool loaded_ = false;
  String error_;

  static uint16_t readU16(const uint8_t* data);
  static int16_t readI16(const uint8_t* data);
  static uint32_t readU32(const uint8_t* data);
  static bool nextCodepoint(const char*& cursor, uint32_t& codepoint);

  bool readAt(uint32_t offset, void* destination, size_t bytes);
  int32_t findGlyphIndex(uint32_t codepoint) const;
  bool readGlyph(uint32_t glyphIndex, Glyph& glyph);
  bool ensureBitmapCapacity(size_t bytes);
  void drawGlyph(int16_t cursorX, int16_t baseline, const Glyph& glyph);
  void fail(const String& message);
};
