#include <GfxRenderer.h>
#include <SloppyDigits.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

uint64_t hashLines(const std::vector<GfxRenderer::Line>& lines) {
  uint64_t hash = 1469598103934665603ULL;
  for (const auto& line : lines) {
    const int values[] = {line.x0, line.y0, line.x1, line.y1, line.state ? 1 : 0};
    for (const int value : values) {
      uint32_t bits = static_cast<uint32_t>(value);
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= static_cast<uint8_t>(bits >> (byte * 8));
        hash *= 1099511628211ULL;
      }
    }
  }
  return hash;
}

TEST(SloppyDigitsTest, StyleAndSeedsAreDeterministic) {
  sloppy::Style firstStyle{};
  sloppy::Style secondStyle{};
  sloppy::Style otherStyle{};
  sloppy::rollStyle(0x12345678u, firstStyle);
  sloppy::rollStyle(0x12345678u, secondStyle);
  sloppy::rollStyle(0x87654321u, otherStyle);
  EXPECT_EQ(std::memcmp(&firstStyle, &secondStyle, sizeof(firstStyle)), 0);
  EXPECT_NE(std::memcmp(&firstStyle, &otherStyle, sizeof(firstStyle)), 0);

  sloppy::Seeds firstSeeds{};
  sloppy::Seeds secondSeeds{};
  sloppy::Seeds otherSeeds{};
  sloppy::prepareSeeds(0x12345678u, firstStyle, firstSeeds);
  sloppy::prepareSeeds(0x12345678u, firstStyle, secondSeeds);
  sloppy::prepareSeeds(0x87654321u, firstStyle, otherSeeds);
  EXPECT_EQ(std::memcmp(&firstSeeds, &secondSeeds, sizeof(firstSeeds)), 0);
  EXPECT_NE(std::memcmp(&firstSeeds, &otherSeeds, sizeof(firstSeeds)), 0);
}

TEST(SloppyDigitsTest, IgnoresSeparatorsAndFitsTenDigits) {
  constexpr sloppy::Style style{sloppy::AlphabetId::Geometric, 0.0f, 3, 0.0f, 0.0f, 12, false};
  sloppy::Seeds seeds{};
  sloppy::prepareSeeds(1u, style, seeds);

  GfxRenderer plain(528, 792);
  GfxRenderer separated(528, 792);
  const sloppy::Bounds bounds{10, 20, 508, 180};
  sloppy::draw(plain, style, seeds, "0123456789", bounds);
  sloppy::draw(separated, style, seeds, "0a1 2-3:4/5_6.7x8+9", bounds);
  EXPECT_EQ(plain.lines, separated.lines);
  ASSERT_FALSE(plain.lines.empty());
  for (const auto& line : plain.lines) {
    EXPECT_GE(line.x0, 0);
    EXPECT_LT(line.x0, plain.getScreenWidth());
    EXPECT_GE(line.y0, 0);
    EXPECT_LT(line.y0, plain.getScreenHeight());
    EXPECT_GE(line.x1, 0);
    EXPECT_LT(line.x1, plain.getScreenWidth());
    EXPECT_GE(line.y1, 0);
    EXPECT_LT(line.y1, plain.getScreenHeight());
  }
}

TEST(SloppyDigitsTest, KeepsClockAndCalendarLayoutsStable) {
  sloppy::Style clockStyle{};
  sloppy::Seeds clockSeeds{};
  sloppy::rollStyle(0x12345678u, clockStyle);
  sloppy::prepareSeeds(0x12345678u, clockStyle, clockSeeds);
  GfxRenderer clock(480, 800);
  sloppy::draw(clock, clockStyle, clockSeeds, "12\n34", {0, 0, 480, 800});
  EXPECT_EQ(hashLines(clock.lines), 13967294608112392867ULL);

  constexpr sloppy::Style calendarStyle{sloppy::AlphabetId::Geometric, 0.0f, 7, 0.0f, 0.0f, 18, false};
  sloppy::Seeds calendarSeeds{};
  sloppy::prepareSeeds(1u, calendarStyle, calendarSeeds);
  GfxRenderer calendar(480, 800);
  sloppy::draw(calendar, calendarStyle, calendarSeeds, "18", {84, 144, 312, 240});
  EXPECT_EQ(hashLines(calendar.lines), 9452051432595563108ULL);
}

TEST(SloppyDigitsTest, SupportsFourRows) {
  constexpr sloppy::Style style{sloppy::AlphabetId::Marker, 4.0f, 3, 5.0f, 3.0f, 16, false};
  sloppy::Seeds seeds{};
  sloppy::prepareSeeds(42u, style, seeds);
  GfxRenderer renderer(480, 800);
  sloppy::draw(renderer, style, seeds, "12\n34\n56\n78", {0, 0, 480, 800});
  EXPECT_FALSE(renderer.lines.empty());
}

}  // namespace
