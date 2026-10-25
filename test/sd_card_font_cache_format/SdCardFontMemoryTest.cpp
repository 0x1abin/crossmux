#include <Arduino.h>
#include <EpdFont.h>
#include <EpdFontData.h>
#include <MissingGlyph.h>
#include <SdCardFontCache.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#define private public
#include <SdCardFont.h>
#undef private

namespace SdCardFontCache {
std::vector<uint8_t> flashBytes;
bool failFlashRead = false;
bool isValidFor(const char*, size_t* size) {
  if (flashBytes.empty()) return false;
  *size = flashBytes.size();
  return true;
}
bool readAt(size_t offset, void* out, size_t bytes, size_t) {
  if (failFlashRead || offset > flashBytes.size() || bytes > flashBytes.size() - offset) return false;
  std::memcpy(out, flashBytes.data() + offset, bytes);
  return true;
}
}  // namespace SdCardFontCache

namespace {
size_t arrayAllocations = 0;
size_t failArraySize = 0;
size_t failArrayAt = 0;
}  // namespace
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  ++arrayAllocations;
  if (size == failArraySize || arrayAllocations == failArrayAt) return nullptr;
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}

class SdCardFontMemoryTest : public ::testing::Test {
 protected:
  SdCardFont font;
  std::string path =
      (std::filesystem::temp_directory_path() / ("crossmux-font-memory-" + std::to_string(getpid()) + ".cpfont"))
          .string();
  void SetUp() override {
    ESP = {};
    font.loaded_ = true;
    std::strncpy(font.filePath_, path.c_str(), sizeof(font.filePath_) - 1);
    font.styles_ = std::make_unique<SdCardFont::PerStyle[]>(1);
    font.styleCount_ = 1;
    font.styleSlots_[0] = 1;
    auto& style = font.styleData(0);
    style.header.intervalCount = 1;
    style.header.glyphCount = 2;
    style.fullIntervals = new EpdUnicodeInterval[1]{{'A', 'B', 0}};
    style.bitmapFileOffset = 2 * sizeof(EpdGlyph);
    font.applyGlyphMissCallback(0);
    style.epdFont.data = &style.stubData;
    EpdGlyph glyphs[2]{};
    glyphs[0].advanceX = 9;
    glyphs[1].advanceX = 13;
    glyphs[0].width = glyphs[1].width = 8;
    glyphs[0].height = glyphs[1].height = 16;
    glyphs[0].dataLength = glyphs[1].dataLength = 16;
    font.fileSize_ = sizeof(glyphs) + 32;
    glyphs[1].dataOffset = 16;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(glyphs), sizeof(glyphs));
    const char bitmaps[32] = {};
    file.write(bitmaps, sizeof(bitmaps));
    arrayAllocations = 0;
  }
  void TearDown() override {
    failArraySize = 0;
    failArrayAt = 0;
    SdCardFontCache::flashBytes.clear();
    SdCardFontCache::failFlashRead = false;
    ESP = {};
    std::filesystem::remove(path);
  }
};

TEST_F(SdCardFontMemoryTest, LowBudgetSkipsAllPrewarmAllocationsAndKeepsMetrics) {
  ESP.freeHeap = 16 * 1024;
  ESP.maxAlloc = 8 * 1024;
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(font.buildAdvanceTable("AB", 1), SdCardFont::PREWARM_SKIPPED);
    EXPECT_EQ(font.prewarm("AB", 1, false, false), SdCardFont::PREWARM_SKIPPED);
  }
  EXPECT_EQ(arrayAllocations, 0U);
  EXPECT_EQ(font.getAdvanceOrLoad('A', 0), 9);
  EXPECT_EQ(font.getAdvanceOrLoad('B', 0), 13);
  ESP = {};
  EXPECT_EQ(font.buildAdvanceTable("AB", 1), 0);
  EXPECT_EQ(font.getAdvance('A', 0), 9);
  EXPECT_EQ(font.getAdvance('B', 0), 13);
}

TEST_F(SdCardFontMemoryTest, MiniGrowthFailurePreservesOldGlyphsAndBitmap) {
  ASSERT_EQ(font.prewarm("A", 1, false, false), 0);
  const auto* data = font.styles_[0].epdFont.data;
  const auto* bitmap = data->bitmap;
  const auto* glyph = data->glyph;
  ASSERT_EQ(data->intervalCount, 1U);
  failArraySize = 2 * sizeof(EpdGlyph);
  EXPECT_LT(font.prewarm("AB", 1, false, false), 0);
  EXPECT_EQ(font.styles_[0].epdFont.data, data);
  EXPECT_EQ(data->bitmap, bitmap);
  EXPECT_EQ(data->glyph, glyph);
  EXPECT_EQ(data->glyph[0].advanceX, 9);
  EXPECT_EQ(data->intervalCount, 1U);
}

TEST_F(SdCardFontMemoryTest, EveryReplacementAllocationFailureKeepsPublishedCache) {
  ASSERT_EQ(font.prewarm("A", 1, false, false), 0);
  auto& style = font.styles_[0];
  const auto* data = style.epdFont.data;
  const auto* intervals = data->intervals;
  const auto* glyphs = data->glyph;
  const auto* bitmap = data->bitmap;
  const uint32_t cps[] = {'A', 'B'};
  // Replacement intervals, glyphs, bitmap and mappings; accumulate=false needs no union.
  for (size_t allocation = 1; allocation <= 4; ++allocation) {
    SCOPED_TRACE(allocation);
    arrayAllocations = 0;
    failArrayAt = allocation;
    EXPECT_LT(font.prewarmStyle(0, cps, 2, false, false, false), 0);
    EXPECT_EQ(arrayAllocations, allocation);  // No later allocations after failure.
    EXPECT_EQ(style.epdFont.data, data);
    EXPECT_EQ(data->intervals, intervals);
    EXPECT_EQ(data->glyph, glyphs);
    EXPECT_EQ(data->bitmap, bitmap);
    EXPECT_EQ(data->intervalCount, 1U);
    EXPECT_EQ(data->glyph[0].advanceX, 9);
  }
  failArrayAt = 0;
  EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false, false), 0);
  EXPECT_EQ(style.miniGlyphCount, 2U);
  EXPECT_EQ(style.miniGlyphs[1].advanceX, 13);
  arrayAllocations = 0;
  for (int batch = 0; batch < 3; ++batch) EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false, false), 0);
  EXPECT_EQ(arrayAllocations, 0U);
}

TEST_F(SdCardFontMemoryTest, AdvanceGrowthFailurePreservesOldTable) {
  ASSERT_EQ(font.buildAdvanceTable("A", 1), 0);
  auto* table = font.advanceTable_[0];
  std::vector<SdCardFont::AdvanceEntry> entries(font.advanceTableCapacity_[0]);
  for (size_t i = 0; i < entries.size(); ++i) entries[i] = {static_cast<uint32_t>('B' + i), 13};
  const size_t allocationsBefore = arrayAllocations;
  failArrayAt = allocationsBefore + 1;  // Fail the preferred capacity growth.
  failArraySize = (font.advanceTableSize_[0] + entries.size()) * sizeof(SdCardFont::AdvanceEntry);
  font.mergeIntoAdvanceTable(0, entries.data(), entries.size());  // Exact-size retry fails too.
  EXPECT_EQ(arrayAllocations, allocationsBefore + 2);
  EXPECT_EQ(font.advanceTable_[0], table);
  EXPECT_EQ(font.advanceTableSize_[0], 1U);
  EXPECT_EQ(font.getAdvance('A', 0), 9);
  EXPECT_EQ(font.getAdvance('B', 0), 0);
  EXPECT_EQ(font.getAdvanceOrLoad('B', 0), 13);
}

TEST_F(SdCardFontMemoryTest, LowBudgetSkipsOptionalKernIndexAndStreamingStillWorks) {
  constexpr size_t glyphOffset = 64 + sizeof(EpdUnicodeInterval);
  constexpr size_t kernOffset = glyphOffset + 2 * sizeof(EpdGlyph);
  constexpr size_t bitmapOffset = kernOffset + 2 * sizeof(EpdKernClassEntry) + 1;
  std::vector<uint8_t> bytes(bitmapOffset + 32, 0);
  const auto put = [&bytes](size_t at, uint32_t value, size_t size) {
    for (size_t i = 0; i < size; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
  };
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  put(8, CPFONT_VERSION, 2);
  bytes[12] = 1;
  put(36, 1, 4);
  put(40, 2, 4);
  bytes[44] = 12;
  put(45, 12, 2);
  put(49, 1, 2);
  put(51, 1, 2);
  bytes[53] = bytes[54] = 1;
  put(56, 64, 4);
  put(64, 'A', 4);
  put(68, 'B', 4);
  for (size_t i = 0; i < 2; ++i) {
    EpdGlyph glyph{8, 8, 128, 0, 8, 16, static_cast<uint32_t>(i * 16)};
    std::memcpy(bytes.data() + glyphOffset + i * sizeof(glyph), &glyph, sizeof(glyph));
  }
  put(kernOffset, 'A', 2);
  bytes[kernOffset + 2] = 1;
  put(kernOffset + 3, 'B', 2);
  bytes[kernOffset + 5] = 1;
  bytes[kernOffset + 6] = static_cast<uint8_t>(-3);
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  ESP.freeHeap = 16 * 1024;
  ESP.maxAlloc = 8 * 1024;
  ASSERT_TRUE(font.load(path.c_str()));
  EXPECT_EQ(font.styles_[0].kernBlockIndex, nullptr);
  ESP = {};
  ASSERT_EQ(font.prewarm("AB", 1, false, true, false), 0);
  const auto* data = font.getEpdFont()->data;
  ASSERT_NE(data->kernMatrix, nullptr);
  EXPECT_EQ(data->kernLeftClassCount, 1);
  EXPECT_EQ(data->kernRightClassCount, 1);
  EXPECT_EQ(data->kernMatrix[0], -3);
}

TEST_F(SdCardFontMemoryTest, PeakBudgetRejectsMiniGrowthBeforeAllocatingAndKeepsOldCache) {
  ASSERT_EQ(font.prewarm("A", 1, false, false), 0);
  const uint32_t cps[] = {'A', 'B'};
  auto* bitmap = font.styles_[0].miniBitmap;
  ESP.freeHeap = 16 * 1024 + 16;
  ESP.maxAlloc = 8 * 1024 + 16;
  arrayAllocations = 0;
  EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false, false), SdCardFont::PREWARM_SKIPPED);
  EXPECT_EQ(font.styles_[0].miniBitmap, bitmap);
  // The optional union can fit; the larger combined replacement peak cannot.
  EXPECT_LE(arrayAllocations, 1U);
}

TEST_F(SdCardFontMemoryTest, ReleasingResidentCachesClearsPublishedLigaturePointers) {
  auto& style = font.styles_[0];
  style.ligaturePairs = new EpdLigaturePair[1]{};
  style.stubData.ligaturePairs = style.miniData.ligaturePairs = style.ligaturePairs;
  style.stubData.ligaturePairCount = style.miniData.ligaturePairCount = 1;
  font.releaseResidentCaches();
  EXPECT_EQ(style.stubData.ligaturePairs, nullptr);
  EXPECT_EQ(style.miniData.ligaturePairs, nullptr);
  EXPECT_EQ(style.stubData.ligaturePairCount, 0);
  EXPECT_EQ(style.epdFont.data, &style.stubData);
  EXPECT_NE(SdCardFont::onGlyphMiss(&font.overflowCtx_[0], 'B'), nullptr);
  EXPECT_EQ(font.getAdvanceOrLoad('B', 0), 13);
}

#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
namespace {
std::vector<uint8_t> pagedFontBytes(uint32_t count = 4001, uint32_t first = 0x10000) {
  std::vector<uint8_t> bytes(64 + count * sizeof(EpdUnicodeInterval) + count * 2 * sizeof(EpdGlyph), 0);
  const auto put = [&bytes](size_t at, uint32_t value, size_t size) {
    for (size_t i = 0; i < size; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (i * 8));
  };
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  put(8, CPFONT_VERSION, 2);
  bytes[12] = 1;
  put(36, count, 4);
  put(40, count * 2, 4);
  bytes[44] = 18;
  put(56, 64, 4);
  for (uint32_t i = 0; i < count; ++i) {
    const size_t at = 64 + i * sizeof(EpdUnicodeInterval);
    put(at, first + i * 4, 4);
    put(at + 4, first + i * 4 + 1, 4);
    put(at + 8, i * 2, 4);
    for (uint32_t j = 0; j < 2; ++j) {
      EpdGlyph glyph{};
      glyph.advanceX = 9 + j;
      std::memcpy(bytes.data() + 64 + count * sizeof(EpdUnicodeInterval) + (i * 2 + j) * sizeof(EpdGlyph), &glyph,
                  sizeof(glyph));
    }
  }
  return bytes;
}
void writeFontBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}  // namespace

TEST_F(SdCardFontMemoryTest, PagedIndexMatchesFullLookupIncludingNonBmpAndPartialLastPage) {
  writeFontBytes(path, pagedFontBytes());
  failArraySize = 4001 * sizeof(EpdUnicodeInterval);
  ASSERT_TRUE(font.load(path.c_str()));
  auto& style = font.styles_[0];
  ASSERT_NE(style.intervalPageStarts, nullptr);
  EXPECT_EQ(style.fullIntervals, nullptr);
  EXPECT_EQ(style.bmpIntervals, nullptr);
  EXPECT_EQ(font.findGlobalGlyphIndex(style, 0xffff), -1);
  arrayAllocations = 0;
  for (uint32_t i = 0; i < 4001; ++i) {
    for (uint32_t j = 0; j < 4; ++j) {
      EXPECT_EQ(font.findGlobalGlyphIndex(style, 0x10000 + i * 4 + j), j < 2 ? static_cast<int32_t>(i * 2 + j) : -1);
    }
  }
  EXPECT_EQ(arrayAllocations, 0U);
  EXPECT_TRUE(SdCardFont::onCoverageQuery(&font.overflowCtx_[0], 0x10000));
  EXPECT_EQ(font.getAdvanceOrLoad(0x10001, 0), 10);
  const auto* glyph = SdCardFont::onGlyphMiss(&font.overflowCtx_[0], 0x10001);
  ASSERT_NE(glyph, nullptr);
  EXPECT_EQ(glyph->advanceX, 10);
  const uint32_t cps[] = {0x10000, 0x10001, 0x10080, 0x13e80};
  EXPECT_EQ(font.prewarmStyle(0, cps, 4, true, false, false), 0);
  EXPECT_EQ(font.styles_[0].miniGlyphCount, 4U);
  font.releaseResidentCaches();
  EXPECT_TRUE(style.hasCoverageIndex());
  EXPECT_EQ(font.getAdvanceOrLoad(0x10001, 0), 10);
}

TEST_F(SdCardFontMemoryTest, PagedIndexShortReadIsRetryableAndNotMissingCoverage) {
  const auto bytes = pagedFontBytes();
  writeFontBytes(path, bytes);
  ASSERT_TRUE(font.load(path.c_str()));
  std::filesystem::resize_file(path, 70);
  EXPECT_EQ(font.findGlobalGlyphIndex(font.styles_[0], 0x10000), -2);
  EXPECT_EQ(font.styles_[0].cachedIntervalPage, -1);
  const uint32_t cp = 0x10000;
  EXPECT_EQ(font.prewarmStyle(0, &cp, 1, true, false, false), -1);
  EXPECT_EQ(font.getAdvanceOrLoad(cp, 0), 0);
  std::string text;
  utf8AppendCodepoint(cp, text);
  EXPECT_LT(font.buildAdvanceTable(text.c_str(), 1), 0);
  EXPECT_FALSE(font.advanceTableLookup(0, cp, nullptr));
  writeFontBytes(path, bytes);
  EXPECT_EQ(font.findGlobalGlyphIndex(font.styles_[0], cp), 0);
  EXPECT_EQ(font.getAdvanceOrLoad(cp, 0), 9);
}

TEST_F(SdCardFontMemoryTest, PagedIndexFallsBackFromFlashToSd) {
  SdCardFontCache::flashBytes = pagedFontBytes();
  writeFontBytes(path, SdCardFontCache::flashBytes);
  ASSERT_TRUE(font.load(path.c_str(), true));
  ASSERT_TRUE(font.usingFlash());
  EXPECT_EQ(font.findGlobalGlyphIndex(font.styles_[0], 0x10000), 0);
  SdCardFontCache::failFlashRead = true;
  EXPECT_EQ(font.findGlobalGlyphIndex(font.styles_[0], 0x10080), 64);
  EXPECT_FALSE(font.usingFlash());
}

TEST_F(SdCardFontMemoryTest, PagedIndexRejectsCorruptIntervalsAndReleasesFailedAllocations) {
  auto bytes = pagedFontBytes();
  bytes[64 + 8] = 7;  // first glyph offset must be zero
  writeFontBytes(path, bytes);
  EXPECT_FALSE(font.load(path.c_str()));
  EXPECT_FALSE(font.loaded_);
  writeFontBytes(path, pagedFontBytes());
  for (const size_t bytesToFail : {size_t(126 * 4), size_t(32 * sizeof(EpdUnicodeInterval))}) {
    failArraySize = bytesToFail;
    EXPECT_FALSE(font.load(path.c_str()));
    EXPECT_FALSE(font.hasStyle(0));
    EXPECT_EQ(font.styles_, nullptr);
  }
  failArraySize = 0;
  EXPECT_TRUE(font.load(path.c_str()));
}

TEST_F(SdCardFontMemoryTest, SmallBmpTableRetainsCompactResidentPath) {
  writeFontBytes(path, pagedFontBytes(64, 0x4e00));
  ASSERT_TRUE(font.load(path.c_str()));
  EXPECT_EQ(font.styles_[0].intervalPageStarts, nullptr);
  EXPECT_TRUE(font.styles_[0].intervalsAreBmp16);
  EXPECT_EQ(font.findGlobalGlyphIndex(font.styles_[0], 0x4e81), 65);
}
#endif

TEST_F(SdCardFontMemoryTest, MissingAdvancesMatchWithAndWithoutPrewarm) {
  font.styles_[0].header.ascender = 24;
  EXPECT_EQ(font.getAdvanceOrLoad('Z', 0), 20 * 16);
  EXPECT_EQ(font.getAdvanceOrLoad(0x1F600, 0), 20 * 16);
  EXPECT_EQ(font.buildAdvanceTable("AZ", 1), 1);
  EXPECT_EQ(font.getAdvance('Z', 0), 20 * 16);
  EXPECT_EQ(font.getAdvanceOrLoad('Z', 0), 20 * 16);
  EXPECT_EQ(font.getAdvance('A', 0), 9);
  EXPECT_EQ(font.getAdvanceOrLoad(0x200D, 0), 0);
  EXPECT_EQ(font.getAdvanceOrLoad(0x301, 0), 0);
  EXPECT_EQ(font.getAdvanceOrLoad(0x64E, 0), 0);
}

TEST_F(SdCardFontMemoryTest, MissingAdvanceIgnoresFontReplacementGlyph) {
  auto& style = font.styles_[0];
  delete[] style.fullIntervals;
  style.fullIntervals = new EpdUnicodeInterval[2]{{'A', 'A', 0}, {0xFFFD, 0xFFFD, 1}};
  style.header.intervalCount = 2;
  style.header.ascender = 24;
  EXPECT_EQ(font.buildAdvanceTable("AZ", 1), 1);
  EXPECT_EQ(font.getAdvanceOrLoad('Z', 0), 20 * 16);
  EXPECT_EQ(font.getAdvanceOrLoad(0xFFFD, 0), 13);
}

TEST_F(SdCardFontMemoryTest, FailedMetadataReadIsRetriedInsteadOfCachingMissingAdvance) {
  std::filesystem::remove(path);
  EXPECT_EQ(font.getAdvanceOrLoad('A', 0), 0);
  font.buildAdvanceTable("A", 1);
  EXPECT_FALSE(font.advanceTableLookup(0, 'A', nullptr));
  EpdGlyph glyph{};
  glyph.advanceX = 9;
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&glyph), sizeof(glyph));
  }
  EXPECT_EQ(font.getAdvanceOrLoad('A', 0), 9);
}
