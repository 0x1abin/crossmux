#include <FontAlloc.h>
#include <FontDecompressor.h>
#include <builtinFonts/notosans_cjk_10.h>
#include <builtinFonts/notosans_cjk_12.h>
#include <builtinFonts/notosans_cjk_8.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

bool testHasPsram = false;
// Fonts use raw DEFLATE; checksum-wrapper entrypoints must remain unused.
extern "C" uint32_t uzlib_adler32(const void*, unsigned int, uint32_t) { std::abort(); }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, uint32_t) { std::abort(); }
namespace {
std::map<void*, size_t> allocations;
size_t peakBytes = 0;
size_t allocationCalls = 0;
int failAllocation = -1;

size_t liveBytes() {
  size_t total = 0;
  for (const auto& entry : allocations) total += entry.second;
  return total;
}

void appendUtf8(std::string& text, uint32_t cp) {
  if (cp < 0x80)
    text.push_back(cp);
  else if (cp < 0x800) {
    text.push_back(0xc0 | cp >> 6);
    text.push_back(0x80 | (cp & 63));
  } else {
    text.push_back(0xe0 | cp >> 12);
    text.push_back(0x80 | ((cp >> 6) & 63));
    text.push_back(0x80 | (cp & 63));
  }
}

uint32_t codepointFor(const EpdFontData& data, uint32_t index) {
  for (uint32_t i = 0; i < data.intervalCount; ++i) {
    const auto& interval = data.intervals[i];
    if (index >= interval.offset && index <= interval.offset + interval.last - interval.first)
      return interval.first + index - interval.offset;
  }
  return 0;
}

std::string groupText(const EpdFontData& data, size_t count) {
  std::string text;
  for (size_t i = 0; i < count; ++i) appendUtf8(text, codepointFor(data, data.groups[i].firstGlyphIndex));
  return text;
}

class FontDecompressorTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(allocations.empty());
    testHasPsram = false;
    peakBytes = allocationCalls = 0;
    failAllocation = -1;
  }
  void TearDown() override { EXPECT_TRUE(allocations.empty()); }
};
}  // namespace

extern "C" void* fiFontMalloc(size_t bytes) {
  ++allocationCalls;
  if (failAllocation == 0) return nullptr;
  if (failAllocation > 0) --failAllocation;
  void* result = std::malloc(bytes);
  if (result) allocations[result] = bytes;
  peakBytes = std::max(peakBytes, liveBytes());
  return result;
}
extern "C" void fiFontFree(void* pointer) {
  if (pointer) {
    EXPECT_EQ(allocations.erase(pointer), 1u);
    std::free(pointer);
  }
}

TEST_F(FontDecompressorTest, AllGlyphsHotGroupAndPrewarmProduceIdenticalPackedPixels) {
  const std::pair<const EpdFontData*, uint64_t> fonts[] = {
      {&notosans_cjk_8, 0x441fb4910e79554c},
      {&notosans_cjk_10, 0xd87f5399d483a7a1},
      {&notosans_cjk_12, 0x908578d024412a97},
  };
  for (const auto& [data, expectedHash] : fonts) {
    uint64_t pixelHash = 14695981039346656037ull;
    FontDecompressor decoder;
    ASSERT_TRUE(decoder.init());
    for (uint16_t groupIndex = 0; groupIndex < data->groupCount; ++groupIndex) {
      const auto& group = data->groups[groupIndex];
      ASSERT_LE(group.uncompressedSize, 4096u);
      std::vector<std::vector<uint8_t>> expected;
      std::string text;
      for (uint32_t i = group.firstGlyphIndex; i < group.firstGlyphIndex + group.glyphCount; ++i) {
        const auto& glyph = data->glyph[i];
        if (glyph.dataLength == 0) {
          expected.emplace_back();
          appendUtf8(text, codepointFor(*data, i));
          continue;
        }
        const auto* pixels = decoder.getBitmap(data, &glyph, i);
        ASSERT_NE(pixels, nullptr);
        for (uint16_t p = 0; p < glyph.dataLength; ++p) pixelHash = (pixelHash ^ pixels[p]) * 1099511628211ull;
        expected.emplace_back(pixels, pixels + glyph.dataLength);
        appendUtf8(text, codepointFor(*data, i));
      }
      const auto callsBefore = allocationCalls;
      decoder.clearCache();
      ASSERT_EQ(decoder.prewarmCache(data, text.c_str()), 0);
      const auto callsAfter = allocationCalls;
      ASSERT_LE(callsAfter - callsBefore, 3u);  // page, index and one reusable hot group
      for (uint32_t i = group.firstGlyphIndex; i < group.firstGlyphIndex + group.glyphCount; ++i) {
        const auto& glyph = data->glyph[i];
        if (glyph.dataLength == 0) continue;
        const auto* pixels = decoder.getBitmap(data, &glyph, i);
        ASSERT_NE(pixels, nullptr);
        EXPECT_EQ(std::memcmp(pixels, expected[i - group.firstGlyphIndex].data(), glyph.dataLength), 0);
      }
      EXPECT_EQ(allocationCalls, callsAfter);
      EXPECT_EQ(decoder.getStats().peakTempBytes, 0u);
      decoder.clearCache();
    }
    EXPECT_EQ(pixelHash, expectedHash);  // raw 441a854b glyph pixels, independent of this decoder
  }
}

TEST_F(FontDecompressorTest, NoPsramPageBudgetFallsBackAndSurvivesStatsReset) {
  FontDecompressor decoder;
  const auto& data = notosans_cjk_12;
  const auto text = groupText(data, data.groupCount);
  EXPECT_GT(decoder.prewarmCache(&data, text.c_str()), 0);
  EXPECT_EQ(allocationCalls, 0u);
  const auto index = data.groups[120].firstGlyphIndex;
  ASSERT_NE(decoder.getBitmap(&data, &data.glyph[index], index), nullptr);
  EXPECT_LE(peakBytes, 4096u + 240u);
  decoder.clearCache();
  const auto small = groupText(data, 45);
  for (int i = 0; i < 4; ++i) {
    decoder.prewarmCache(&data, small.c_str());
    EXPECT_LE(liveBytes(), 16384u + 4096u);
    decoder.resetStats();
  }
  decoder.clearCache();
  EXPECT_EQ(liveBytes(), 0u);
  EXPECT_EQ(decoder.prewarmCache(&data, small.c_str()), 0);
}

TEST_F(FontDecompressorTest, PsramGroupsPast128AndMissingCacheEntriesUseHotFallback) {
  testHasPsram = true;
  FontDecompressor decoder;
  const auto& data = notosans_cjk_12;
  ASSERT_GT(data.groupCount, 128u);
  const auto text = groupText(data, data.groupCount);
  decoder.prewarmCache(&data, text.c_str());
  EXPECT_GT(liveBytes(), 16384u);
  for (uint16_t g = 0; g < data.groupCount; ++g) {
    const auto index = data.groups[g].firstGlyphIndex;
    EXPECT_NE(decoder.getBitmap(&data, &data.glyph[index], index), nullptr);
  }
}

TEST_F(FontDecompressorTest, AllocationFailuresReleasePartialCachesAndPermitRetry) {
  for (int failure = 0; failure < 4; ++failure) {
    FontDecompressor decoder;
    failAllocation = failure;
    const auto text = groupText(notosans_cjk_12, 4);
    decoder.prewarmCache(&notosans_cjk_12, text.c_str());
    decoder.clearCache();
    EXPECT_EQ(liveBytes(), 0u);
    failAllocation = -1;
    const auto index = notosans_cjk_12.groups[3].firstGlyphIndex;
    ASSERT_NE(decoder.getBitmap(&notosans_cjk_12, &notosans_cjk_12.glyph[index], index), nullptr);
  }
}

TEST_F(FontDecompressorTest, HotGroupAndGlyphAllocationFailureDoNotPublishStalePixels) {
  FontDecompressor decoder;
  const auto& data = notosans_cjk_12;
  const auto index = data.groups[3].firstGlyphIndex;
  for (int failure : {0, 1}) {
    failAllocation = failure;
    EXPECT_EQ(decoder.getBitmap(&data, &data.glyph[index], index), nullptr);
    decoder.clearCache();
    EXPECT_EQ(liveBytes(), 0u);
  }
  failAllocation = -1;
  ASSERT_NE(decoder.getBitmap(&data, &data.glyph[index], index), nullptr);
}
