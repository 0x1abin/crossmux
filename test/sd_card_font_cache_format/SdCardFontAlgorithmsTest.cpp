#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "EpdFont/SdCardFontAlgorithms.h"
#include "EpdFont/SdCardFontGlyphCache.h"

namespace {
std::vector<uint32_t> glyphCacheStorage(const size_t bytes = SdCardFontGlyphCache::TOTAL_BYTES) {
  return std::vector<uint32_t>((bytes + sizeof(uint32_t) - 1) / sizeof(uint32_t));
}

EpdGlyph glyphWithBitmap(const uint16_t length, const uint32_t offset = 17) {
  EpdGlyph glyph{};
  glyph.width = 8;
  glyph.height = 12;
  glyph.advanceX = 9 << 4;
  glyph.dataLength = length;
  glyph.dataOffset = offset;
  return glyph;
}
}  // namespace

TEST(SdCardFontAlgorithms, SeparatesBitmapAndKernLigaturePrewarmPolicy) {
  const auto metadataWithKern = sd_card_font_algorithms::prewarmLoadPolicy(true, true);
  EXPECT_FALSE(metadataWithKern.bitmap);
  EXPECT_FALSE(metadataWithKern.kernLigature);

  const auto metadataWithoutKern = sd_card_font_algorithms::prewarmLoadPolicy(true, false);
  EXPECT_FALSE(metadataWithoutKern.bitmap);
  EXPECT_FALSE(metadataWithoutKern.kernLigature);

  const auto fullWithKern = sd_card_font_algorithms::prewarmLoadPolicy(false, true);
  EXPECT_TRUE(fullWithKern.bitmap);
  EXPECT_TRUE(fullWithKern.kernLigature);

  const auto fullWithoutKern = sd_card_font_algorithms::prewarmLoadPolicy(false, false);
  EXPECT_TRUE(fullWithoutKern.bitmap);
  EXPECT_FALSE(fullWithoutKern.kernLigature);
}

TEST(SdCardFontAlgorithms, InsertsSortedUniqueWithinCapacity) {
  uint32_t codepoints[4] = {};
  uint32_t count = 0;
  for (const uint32_t codepoint : {9u, 3u, 9u, 7u, 1u}) {
    EXPECT_TRUE(sd_card_font_algorithms::insertSortedUnique(codepoint, codepoints, count, 4));
  }
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(std::vector<uint32_t>(codepoints, codepoints + count), (std::vector<uint32_t>{1, 3, 7, 9}));

  EXPECT_FALSE(sd_card_font_algorithms::insertSortedUnique(5, codepoints, count, 4));
  EXPECT_EQ(std::vector<uint32_t>(codepoints, codepoints + count), (std::vector<uint32_t>{1, 3, 7, 9}));
}

TEST(SdCardFontAlgorithms, MergesKernClassesAndSupportsRenumbering) {
  const uint32_t codepoints[] = {5, 10, 20, 30, 40, 50};
  const EpdKernClassEntry entries[] = {{10, 3}, {20, 7}, {40, 2}};
  std::vector<std::pair<uint16_t, uint8_t>> matches;

  sd_card_font_algorithms::forEachKernClassMatch(codepoints, 6, entries, 3, [&](const EpdKernClassEntry& entry) {
    matches.emplace_back(entry.codepoint, entry.classId);
  });
  EXPECT_EQ(matches, (std::vector<std::pair<uint16_t, uint8_t>>{{10, 3}, {20, 7}, {40, 2}}));

  const uint8_t renumber[8] = {0, 0, 1, 2, 0, 0, 0, 3};
  matches.clear();
  sd_card_font_algorithms::forEachKernClassMatch(codepoints, 6, entries, 3, [&](const EpdKernClassEntry& entry) {
    matches.emplace_back(entry.codepoint, renumber[entry.classId]);
  });
  EXPECT_EQ(matches, (std::vector<std::pair<uint16_t, uint8_t>>{{10, 2}, {20, 3}, {40, 1}}));
}

TEST(SdCardFontGlyphCache, StoresMetadataAndBitmapByStyle) {
  auto storage = glyphCacheStorage();
  SdCardFontGlyphCache cache;
  cache.reset(reinterpret_cast<uint8_t*>(storage.data()), storage.size() * sizeof(uint32_t));

  const EpdGlyph glyph = glyphWithBitmap(3);
  EXPECT_EQ(cache.storeMetadata(0, 42, glyph), SdCardFontGlyphCache::StoreResult::Stored);

  EpdGlyph loaded{};
  const uint8_t* bitmap = nullptr;
  EXPECT_EQ(cache.lookup(0, 42, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Metadata);
  EXPECT_EQ(loaded.advanceX, glyph.advanceX);
  EXPECT_EQ(cache.lookup(1, 42, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Miss);

  constexpr std::array<uint8_t, 3> pixels{1, 2, 3};
  EXPECT_EQ(cache.storeBitmap(0, 42, glyph, pixels.data()), SdCardFontGlyphCache::StoreResult::Stored);
  EXPECT_EQ(cache.lookup(0, 42, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Bitmap);
  ASSERT_NE(bitmap, nullptr);
  EXPECT_EQ(std::vector<uint8_t>(bitmap, bitmap + pixels.size()),
            (std::vector<uint8_t>{pixels[0], pixels[1], pixels[2]}));
}

TEST(SdCardFontGlyphCache, TreatsZeroLengthBitmapAsCached) {
  auto storage = glyphCacheStorage();
  SdCardFontGlyphCache cache;
  cache.reset(reinterpret_cast<uint8_t*>(storage.data()), storage.size() * sizeof(uint32_t));

  const EpdGlyph glyph = glyphWithBitmap(0);
  EXPECT_EQ(cache.storeMetadata(2, 9, glyph), SdCardFontGlyphCache::StoreResult::Stored);

  EpdGlyph loaded{};
  const uint8_t* bitmap = reinterpret_cast<const uint8_t*>(1);
  EXPECT_EQ(cache.lookup(2, 9, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Bitmap);
  EXPECT_EQ(bitmap, nullptr);
}

TEST(SdCardFontGlyphCache, StopsAfterFourProbeCollision) {
  auto storage = glyphCacheStorage();
  SdCardFontGlyphCache cache;
  cache.reset(reinterpret_cast<uint8_t*>(storage.data()), storage.size() * sizeof(uint32_t));
  const EpdGlyph glyph = glyphWithBitmap(0);

  for (uint32_t i = 0; i < SdCardFontGlyphCache::PROBE_COUNT; ++i) {
    EXPECT_EQ(cache.storeMetadata(0, i * SdCardFontGlyphCache::ENTRY_COUNT, glyph),
              SdCardFontGlyphCache::StoreResult::Stored);
  }
  EXPECT_EQ(cache.storeMetadata(0, SdCardFontGlyphCache::PROBE_COUNT * SdCardFontGlyphCache::ENTRY_COUNT, glyph),
            SdCardFontGlyphCache::StoreResult::Collision);
}

TEST(SdCardFontGlyphCache, DoesNotPublishBitmapWhenArenaIsFull) {
  auto storage = glyphCacheStorage(SdCardFontGlyphCache::ENTRY_BYTES + 2);
  SdCardFontGlyphCache cache;
  cache.reset(reinterpret_cast<uint8_t*>(storage.data()), SdCardFontGlyphCache::ENTRY_BYTES + 2);
  const EpdGlyph glyph = glyphWithBitmap(3);
  constexpr std::array<uint8_t, 3> pixels{1, 2, 3};

  EXPECT_EQ(cache.storeMetadata(0, 7, glyph), SdCardFontGlyphCache::StoreResult::Stored);
  EXPECT_EQ(cache.storeBitmap(0, 7, glyph, pixels.data()), SdCardFontGlyphCache::StoreResult::Full);

  EpdGlyph loaded{};
  const uint8_t* bitmap = nullptr;
  EXPECT_EQ(cache.lookup(0, 7, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Metadata);
}

TEST(SdCardFontGlyphCache, DoesNotPublishIncompleteBitmap) {
  auto storage = glyphCacheStorage();
  SdCardFontGlyphCache cache;
  cache.reset(reinterpret_cast<uint8_t*>(storage.data()), storage.size() * sizeof(uint32_t));
  const EpdGlyph glyph = glyphWithBitmap(3);

  EXPECT_EQ(cache.storeMetadata(0, 7, glyph), SdCardFontGlyphCache::StoreResult::Stored);
  EXPECT_EQ(cache.storeBitmap(0, 7, glyph, nullptr), SdCardFontGlyphCache::StoreResult::Full);

  EpdGlyph loaded{};
  const uint8_t* bitmap = nullptr;
  EXPECT_EQ(cache.lookup(0, 7, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Metadata);
}

TEST(SdCardFontGlyphCache, DisabledCacheAlwaysFallsBack) {
  SdCardFontGlyphCache cache;
  const EpdGlyph glyph = glyphWithBitmap(1);
  EXPECT_EQ(cache.storeMetadata(0, 1, glyph), SdCardFontGlyphCache::StoreResult::Disabled);

  EpdGlyph loaded{};
  const uint8_t* bitmap = nullptr;
  EXPECT_EQ(cache.lookup(0, 1, loaded, bitmap), SdCardFontGlyphCache::LookupResult::Miss);
}
