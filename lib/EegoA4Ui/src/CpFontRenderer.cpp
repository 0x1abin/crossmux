#include "CpFontRenderer.h"

#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr uint8_t CPFONT_MAGIC[8] = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0};
constexpr uint16_t CPFONT_VERSION = 4;
constexpr uint32_t GLOBAL_HEADER_BYTES = 32;
constexpr uint32_t STYLE_TOC_BYTES = 32;
constexpr uint32_t INTERVAL_BYTES = 12;
constexpr uint32_t GLYPH_BYTES = 16;
constexpr uint32_t KERN_ENTRY_BYTES = 3;
constexpr uint32_t LIGATURE_BYTES = 8;
constexpr uint32_t MAX_INTERVALS = 4096;
constexpr uint32_t MAX_GLYPHS = 65536;

}  // namespace

CpFontRenderer::CpFontRenderer(SDCardManager& sd, PortraitCanvas& canvas)
    : sd_(sd), canvas_(canvas) {}

CpFontRenderer::~CpFontRenderer() {
  unload();
  if (bitmap_) heap_caps_free(bitmap_);
}

uint16_t CpFontRenderer::readU16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         static_cast<uint16_t>(data[1] << 8);
}

int16_t CpFontRenderer::readI16(const uint8_t* data) {
  return static_cast<int16_t>(readU16(data));
}

uint32_t CpFontRenderer::readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void CpFontRenderer::fail(const String& message) {
  error_ = message;
  Serial.printf("[FONT] ERROR: %s\n", error_.c_str());
}

bool CpFontRenderer::readAt(const uint32_t offset, void* destination, const size_t bytes) {
  if (!file_ || !file_.seekSet(offset)) return false;
  return file_.read(static_cast<uint8_t*>(destination), bytes) ==
         static_cast<int>(bytes);
}

void CpFontRenderer::unload() {
  loaded_ = false;
  if (file_) file_.close();
  if (intervals_) {
    heap_caps_free(intervals_);
    intervals_ = nullptr;
  }
  info_ = {};
  glyphsFileOffset_ = 0;
  bitmapFileOffset_ = 0;
}

bool CpFontRenderer::load(const char* path) {
  unload();
  error_ = "";
  if (!sd_.ready()) {
    fail("SD card is not mounted");
    return false;
  }

  file_ = sd_.open(path, O_RDONLY);
  if (!file_) {
    fail(String("cannot open ") + path);
    return false;
  }
  info_.fileBytes = static_cast<uint32_t>(file_.fileSize());

  uint8_t header[GLOBAL_HEADER_BYTES] = {};
  if (!readAt(0, header, sizeof(header))) {
    fail("short CPFONT global header");
    unload();
    return false;
  }
  if (memcmp(header, CPFONT_MAGIC, sizeof(CPFONT_MAGIC)) != 0) {
    fail("invalid CPFONT magic");
    unload();
    return false;
  }
  if (readU16(header + 8) != CPFONT_VERSION) {
    fail(String("unsupported CPFONT version ") + readU16(header + 8));
    unload();
    return false;
  }
  info_.is2Bit = (readU16(header + 10) & 1U) != 0;
  const uint8_t styleCount = header[12];
  if (styleCount == 0 || styleCount > 4) {
    fail(String("invalid style count ") + styleCount);
    unload();
    return false;
  }

  uint8_t chosen[STYLE_TOC_BYTES] = {};
  bool haveStyle = false;
  for (uint8_t index = 0; index < styleCount; ++index) {
    uint8_t toc[STYLE_TOC_BYTES] = {};
    if (!readAt(GLOBAL_HEADER_BYTES + index * STYLE_TOC_BYTES, toc, sizeof(toc))) {
      fail("short CPFONT style table");
      unload();
      return false;
    }
    if (!haveStyle || toc[0] == 0) {
      memcpy(chosen, toc, sizeof(chosen));
      haveStyle = true;
    }
    if (toc[0] == 0) break;
  }

  info_.styleId = chosen[0];
  info_.intervalCount = readU32(chosen + 4);
  info_.glyphCount = readU32(chosen + 8);
  info_.advanceY = chosen[12];
  info_.ascender = readI16(chosen + 13);
  info_.descender = readI16(chosen + 15);
  const uint16_t kernLeftEntries = readU16(chosen + 17);
  const uint16_t kernRightEntries = readU16(chosen + 19);
  const uint8_t kernLeftClasses = chosen[21];
  const uint8_t kernRightClasses = chosen[22];
  const uint8_t ligatureCount = chosen[23];
  const uint32_t dataOffset = readU32(chosen + 24);

  if (info_.intervalCount == 0 || info_.intervalCount > MAX_INTERVALS ||
      info_.glyphCount == 0 || info_.glyphCount > MAX_GLYPHS) {
    fail("unreasonable CPFONT interval/glyph counts");
    unload();
    return false;
  }

  const uint64_t glyphsOffset =
      static_cast<uint64_t>(dataOffset) + info_.intervalCount * INTERVAL_BYTES;
  const uint64_t bitmapOffset =
      glyphsOffset + info_.glyphCount * GLYPH_BYTES +
      static_cast<uint64_t>(kernLeftEntries + kernRightEntries) * KERN_ENTRY_BYTES +
      static_cast<uint64_t>(kernLeftClasses) * kernRightClasses +
      static_cast<uint64_t>(ligatureCount) * LIGATURE_BYTES;
  if (glyphsOffset > UINT32_MAX || bitmapOffset > info_.fileBytes) {
    fail("CPFONT section offsets exceed file size");
    unload();
    return false;
  }
  glyphsFileOffset_ = static_cast<uint32_t>(glyphsOffset);
  bitmapFileOffset_ = static_cast<uint32_t>(bitmapOffset);

  const size_t intervalBytes = info_.intervalCount * sizeof(Interval);
  intervals_ = static_cast<Interval*>(
      heap_caps_malloc(intervalBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!intervals_) {
    intervals_ =
        static_cast<Interval*>(heap_caps_malloc(intervalBytes, MALLOC_CAP_8BIT));
  }
  if (!intervals_) {
    fail(String("cannot allocate ") + intervalBytes + " bytes for coverage index");
    unload();
    return false;
  }
  if (!readAt(dataOffset, intervals_, intervalBytes)) {
    fail("short CPFONT interval table");
    unload();
    return false;
  }

  uint32_t expectedGlyphOffset = 0;
  uint32_t previousLast = 0;
  for (uint32_t index = 0; index < info_.intervalCount; ++index) {
    const Interval& interval = intervals_[index];
    if (interval.first > interval.last ||
        (index && interval.first <= previousLast) ||
        interval.offset != expectedGlyphOffset) {
      fail(String("invalid coverage interval ") + index);
      unload();
      return false;
    }
    const uint64_t span =
        static_cast<uint64_t>(interval.last) - interval.first + 1;
    if (expectedGlyphOffset + span > info_.glyphCount) {
      fail(String("coverage interval overruns glyph table at ") + index);
      unload();
      return false;
    }
    expectedGlyphOffset += static_cast<uint32_t>(span);
    previousLast = interval.last;
  }
  if (expectedGlyphOffset != info_.glyphCount) {
    fail("coverage table does not account for every glyph");
    unload();
    return false;
  }

  loaded_ = true;
  Serial.printf(
      "[FONT] loaded=%s bytes=%lu style=%u intervals=%lu glyphs=%lu "
      "advanceY=%u asc=%d desc=%d depth=%u\n",
      path, static_cast<unsigned long>(info_.fileBytes), info_.styleId,
      static_cast<unsigned long>(info_.intervalCount),
      static_cast<unsigned long>(info_.glyphCount), info_.advanceY,
      info_.ascender, info_.descender, info_.is2Bit ? 2 : 1);
  return true;
}

int32_t CpFontRenderer::findGlyphIndex(const uint32_t codepoint) const {
  if (!loaded_ || !intervals_) return -1;
  int32_t left = 0;
  int32_t right = static_cast<int32_t>(info_.intervalCount) - 1;
  while (left <= right) {
    const int32_t middle = left + (right - left) / 2;
    const Interval& interval = intervals_[middle];
    if (codepoint < interval.first) {
      right = middle - 1;
    } else if (codepoint > interval.last) {
      left = middle + 1;
    } else {
      return static_cast<int32_t>(
          interval.offset + codepoint - interval.first);
    }
  }
  return -1;
}

bool CpFontRenderer::hasCodepoint(const uint32_t codepoint) const {
  return findGlyphIndex(codepoint) >= 0;
}

bool CpFontRenderer::nextCodepoint(const char*& cursor, uint32_t& codepoint) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(cursor);
  if (!*bytes) return false;

  if (bytes[0] < 0x80) {
    codepoint = bytes[0];
    cursor += 1;
    return true;
  }

  uint8_t length = 0;
  uint32_t value = 0;
  uint32_t minimum = 0;
  if ((bytes[0] & 0xe0U) == 0xc0U) {
    length = 2;
    value = bytes[0] & 0x1fU;
    minimum = 0x80;
  } else if ((bytes[0] & 0xf0U) == 0xe0U) {
    length = 3;
    value = bytes[0] & 0x0fU;
    minimum = 0x800;
  } else if ((bytes[0] & 0xf8U) == 0xf0U) {
    length = 4;
    value = bytes[0] & 0x07U;
    minimum = 0x10000;
  } else {
    codepoint = 0xfffd;
    cursor += 1;
    return true;
  }

  for (uint8_t index = 1; index < length; ++index) {
    if ((bytes[index] & 0xc0U) != 0x80U) {
      codepoint = 0xfffd;
      cursor += 1;
      return true;
    }
    value = (value << 6) | (bytes[index] & 0x3fU);
  }
  if (value < minimum || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) {
    codepoint = 0xfffd;
    cursor += 1;
    return true;
  }
  codepoint = value;
  cursor += length;
  return true;
}

uint16_t CpFontRenderer::countMissing(const char* utf8Text) const {
  if (!loaded_ || !utf8Text) return 0xffff;
  uint16_t missing = 0;
  const char* cursor = utf8Text;
  uint32_t codepoint = 0;
  while (nextCodepoint(cursor, codepoint)) {
    if (!hasCodepoint(codepoint)) ++missing;
  }
  return missing;
}

bool CpFontRenderer::readGlyph(const uint32_t glyphIndex, Glyph& glyph) {
  if (glyphIndex >= info_.glyphCount) return false;
  uint8_t raw[GLYPH_BYTES] = {};
  if (!readAt(glyphsFileOffset_ + glyphIndex * GLYPH_BYTES, raw, sizeof(raw))) {
    return false;
  }
  glyph.width = raw[0];
  glyph.height = raw[1];
  glyph.advanceX = readU16(raw + 2);
  glyph.left = readI16(raw + 4);
  glyph.top = readI16(raw + 6);
  glyph.dataLength = readU16(raw + 8);
  glyph.dataOffset = readU32(raw + 12);
  return static_cast<uint64_t>(bitmapFileOffset_) + glyph.dataOffset +
             glyph.dataLength <=
         info_.fileBytes;
}

bool CpFontRenderer::ensureBitmapCapacity(const size_t bytes) {
  if (bytes <= bitmapCapacity_) return true;
  size_t capacity = std::max<size_t>(bytes, bitmapCapacity_ * 2);
  if (capacity < 256) capacity = 256;
  auto* replacement = static_cast<uint8_t*>(
      heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!replacement) {
    replacement =
        static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_8BIT));
  }
  if (!replacement) return false;
  if (bitmap_) heap_caps_free(bitmap_);
  bitmap_ = replacement;
  bitmapCapacity_ = capacity;
  return true;
}

void CpFontRenderer::drawGlyph(const int16_t cursorX, const int16_t baseline,
                               const Glyph& glyph) {
  const int16_t originX = cursorX + glyph.left;
  const int16_t originY = baseline - glyph.top;
  const uint32_t pixels =
      static_cast<uint32_t>(glyph.width) * glyph.height;
  for (uint32_t position = 0; position < pixels; ++position) {
    bool ink = false;
    if (info_.is2Bit) {
      const uint8_t packed = bitmap_[position >> 2];
      const uint8_t shift = static_cast<uint8_t>((3 - (position & 3U)) * 2);
      ink = ((packed >> shift) & 0x03U) != 0;
    } else {
      const uint8_t packed = bitmap_[position >> 3];
      ink = ((packed >> (7 - (position & 7U))) & 0x01U) != 0;
    }
    if (!ink) continue;
    const int16_t glyphX = static_cast<int16_t>(position % glyph.width);
    const int16_t glyphY = static_cast<int16_t>(position / glyph.width);
    canvas_.drawPixel(originX + glyphX, originY + glyphY, 0);
  }
}

int16_t CpFontRenderer::drawText(const int16_t x, const int16_t top,
                                 const char* utf8Text, uint16_t* missing,
                                 uint16_t* ioErrors) {
  uint16_t missingCount = 0;
  uint16_t errorCount = 0;
  int16_t cursorX = x;
  const int16_t baseline = top + info_.ascender;
  if (!loaded_ || !utf8Text) {
    if (missing) *missing = 0;
    if (ioErrors) *ioErrors = 1;
    return cursorX;
  }

  const char* cursor = utf8Text;
  uint32_t codepoint = 0;
  while (nextCodepoint(cursor, codepoint)) {
    const int32_t glyphIndex = findGlyphIndex(codepoint);
    if (glyphIndex < 0) {
      ++missingCount;
      continue;
    }
    Glyph glyph;
    if (!readGlyph(static_cast<uint32_t>(glyphIndex), glyph) ||
        !ensureBitmapCapacity(glyph.dataLength) ||
        (glyph.dataLength &&
         !readAt(bitmapFileOffset_ + glyph.dataOffset, bitmap_,
                 glyph.dataLength))) {
      ++errorCount;
      continue;
    }
    if (glyph.dataLength) drawGlyph(cursorX, baseline, glyph);
    cursorX = static_cast<int16_t>(
        cursorX + ((static_cast<uint32_t>(glyph.advanceX) + 8U) >> 4));
  }

  if (missing) *missing = missingCount;
  if (ioErrors) *ioErrors = errorCount;
  return cursorX;
}
