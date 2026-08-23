#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "EpdFont/SdCardFontAlgorithms.h"

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
