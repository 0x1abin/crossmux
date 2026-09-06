#include <Arduino.h>
#include <EpdFont.h>
#include <EpdFontData.h>
#include <SdCardFontCache.h>
#include <gtest/gtest.h>

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
bool isValidFor(const char*, size_t*) { return false; }
bool readAt(size_t, void*, size_t, size_t) { return false; }
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
  std::string path = (std::filesystem::temp_directory_path() / "crossmux-font-memory.cpfont").string();
  void SetUp() override {
    ESP = {};
    font.loaded_ = true;
    std::strncpy(font.filePath_, path.c_str(), sizeof(font.filePath_) - 1);
    auto& style = font.styles_[0];
    style.present = true;
    style.header.intervalCount = 1;
    style.header.glyphCount = 2;
    style.fullIntervals = new EpdUnicodeInterval[1]{{'A', 'B', 0}};
    style.bitmapFileOffset = 2 * sizeof(EpdGlyph);
    font.applyGlyphMissCallback(0);
    style.epdFont.data = &style.stubData;
    EpdGlyph glyphs[2]{};
    glyphs[0].advanceX = 9;
    glyphs[1].advanceX = 13;
    glyphs[0].dataLength = glyphs[1].dataLength = 16;
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
  ASSERT_EQ(font.prewarm("A", 1, false, false), 1);  // fixture has no replacement glyph
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
  ASSERT_EQ(font.prewarm("A", 1, false, false), 1);
  auto& style = font.styles_[0];
  const auto* data = style.epdFont.data;
  const auto* intervals = data->intervals;
  const auto* glyphs = data->glyph;
  const auto* bitmap = data->bitmap;
  const uint32_t cps[] = {'A', 'B'};
  // Union first, then replacement intervals, glyphs, bitmap and mappings.
  for (size_t allocation = 2; allocation <= 5; ++allocation) {
    SCOPED_TRACE(allocation);
    arrayAllocations = 0;
    failArrayAt = allocation;
    EXPECT_LT(font.prewarmStyle(0, cps, 2, false, false), 0);
    EXPECT_EQ(arrayAllocations, allocation);  // No later allocations after failure.
    EXPECT_EQ(style.epdFont.data, data);
    EXPECT_EQ(data->intervals, intervals);
    EXPECT_EQ(data->glyph, glyphs);
    EXPECT_EQ(data->bitmap, bitmap);
    EXPECT_EQ(data->intervalCount, 1U);
    EXPECT_EQ(data->glyph[0].advanceX, 9);
  }
  failArrayAt = 0;
  EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false), 0);
  EXPECT_EQ(style.miniGlyphCount, 2U);
  EXPECT_EQ(style.miniGlyphs[1].advanceX, 13);
  arrayAllocations = 0;
  for (int batch = 0; batch < 3; ++batch) EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false), 0);
  EXPECT_EQ(arrayAllocations, 0U);
}

TEST_F(SdCardFontMemoryTest, AdvanceGrowthFailurePreservesOldTable) {
  ASSERT_EQ(font.buildAdvanceTable("A", 1), 0);
  auto* table = font.advanceTable_[0];
  const SdCardFont::AdvanceEntry entry{'B', 13};
  failArraySize = 2 * sizeof(SdCardFont::AdvanceEntry);
  font.mergeIntoAdvanceTable(0, &entry, 1);
  EXPECT_EQ(font.advanceTable_[0], table);
  EXPECT_EQ(font.getAdvance('A', 0), 9);
  EXPECT_EQ(font.getAdvanceOrLoad('B', 0), 13);
}

TEST_F(SdCardFontMemoryTest, PeakBudgetRejectsMiniGrowthBeforeAllocatingAndKeepsOldCache) {
  ASSERT_EQ(font.prewarm("A", 1, false, false), 1);  // fixture has no replacement glyph
  const uint32_t cps[] = {'A', 'B'};
  auto* bitmap = font.styles_[0].miniBitmap;
  ESP.freeHeap = 16 * 1024 + 16;
  ESP.maxAlloc = 8 * 1024 + 16;
  arrayAllocations = 0;
  EXPECT_EQ(font.prewarmStyle(0, cps, 2, false, false), SdCardFont::PREWARM_SKIPPED);
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
