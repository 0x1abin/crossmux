#include <gtest/gtest.h>

#include <vector>

#include "util/ReadingGuideLine.h"

namespace {

struct Segment {
  int x1;
  int y1;
  int x2;
  int y2;
  bool state;

  bool operator==(const Segment&) const = default;
};

struct RecordingRenderer {
  std::vector<Segment> segments;

  void drawLine(const int x1, const int y1, const int x2, const int y2, const bool state) {
    segments.push_back({x1, y1, x2, y2, state});
  }
};

TEST(ReadingGuideLine, MapsLinearStylesAndFallsBackToShortDash) {
  EXPECT_EQ(static_cast<uint8_t>(readingGuideLine::Style::Wavy), 5);
  EXPECT_EQ(readingGuideLine::patternFor(static_cast<uint8_t>(readingGuideLine::Style::Solid)),
            (readingGuideLine::Pattern{0, 0}));
  EXPECT_EQ(readingGuideLine::patternFor(static_cast<uint8_t>(readingGuideLine::Style::ShortDash)),
            (readingGuideLine::Pattern{5, 5}));
  EXPECT_EQ(readingGuideLine::patternFor(static_cast<uint8_t>(readingGuideLine::Style::MediumDash)),
            (readingGuideLine::Pattern{10, 10}));
  EXPECT_EQ(readingGuideLine::patternFor(static_cast<uint8_t>(readingGuideLine::Style::LongDash)),
            (readingGuideLine::Pattern{20, 10}));
  EXPECT_EQ(readingGuideLine::patternFor(static_cast<uint8_t>(readingGuideLine::Style::Dotted)),
            (readingGuideLine::Pattern{2, 5}));
  EXPECT_EQ(readingGuideLine::patternFor(255), (readingGuideLine::Pattern{5, 5}));
}

TEST(ReadingGuideLine, DrawsWavyLineAndClipsPartialFinalSegment) {
  RecordingRenderer renderer;
  readingGuideLine::draw(renderer, 0, 10, 9, static_cast<uint8_t>(readingGuideLine::Style::Wavy));

  EXPECT_EQ(
      renderer.segments,
      (std::vector<Segment>{
          {0, 10, 2, 8, true}, {2, 8, 4, 10, true}, {4, 10, 6, 12, true}, {6, 12, 8, 10, true}, {8, 10, 9, 9, true}}));
}

TEST(ReadingGuideLine, DrawsNarrowAndReversedWavyRanges) {
  RecordingRenderer renderer;
  readingGuideLine::draw(renderer, 5, 4, 5, static_cast<uint8_t>(readingGuideLine::Style::Wavy));
  EXPECT_EQ(renderer.segments, (std::vector<Segment>{{5, 4, 5, 4, true}}));

  renderer.segments.clear();
  readingGuideLine::draw(renderer, 6, 4, 3, static_cast<uint8_t>(readingGuideLine::Style::Wavy));
  EXPECT_EQ(renderer.segments, (std::vector<Segment>{{3, 4, 5, 2, true}, {5, 2, 6, 3, true}}));
}

TEST(ReadingGuideLine, ReportsAndChecksWavyVerticalRadius) {
  const auto wavy = static_cast<uint8_t>(readingGuideLine::Style::Wavy);
  EXPECT_EQ(readingGuideLine::verticalRadius(wavy), 2);
  EXPECT_EQ(readingGuideLine::verticalRadius(static_cast<uint8_t>(readingGuideLine::Style::Dotted)), 0);
  EXPECT_TRUE(readingGuideLine::fitsVertically(wavy, 2, 0, 5));
  EXPECT_FALSE(readingGuideLine::fitsVertically(wavy, 1, 0, 5));
  EXPECT_FALSE(readingGuideLine::fitsVertically(wavy, 3, 0, 5));
}

TEST(ReadingGuideLine, DrawsSolidLineWithNormalizedBounds) {
  RecordingRenderer renderer;
  readingGuideLine::draw(renderer, 12, 7, 3, static_cast<uint8_t>(readingGuideLine::Style::Solid));

  ASSERT_EQ(renderer.segments.size(), 1U);
  EXPECT_EQ(renderer.segments[0], (Segment{3, 7, 12, 7, true}));
}

TEST(ReadingGuideLine, ClipsShortFinalSegmentAndHandlesNarrowRanges) {
  RecordingRenderer renderer;
  readingGuideLine::draw(renderer, 0, 4, 11, static_cast<uint8_t>(readingGuideLine::Style::ShortDash));
  EXPECT_EQ(renderer.segments, (std::vector<Segment>{{0, 4, 4, 4, true}, {10, 4, 11, 4, true}}));

  renderer.segments.clear();
  readingGuideLine::draw(renderer, 8, 2, 9, static_cast<uint8_t>(readingGuideLine::Style::MediumDash));
  EXPECT_EQ(renderer.segments, (std::vector<Segment>{{8, 2, 9, 2, true}}));
}

}  // namespace
