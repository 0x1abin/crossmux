#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace readingGuideLine {

enum class Style : uint8_t { Solid = 0, ShortDash, MediumDash, LongDash, Dotted, Wavy, Count };

constexpr int WAVE_STEP = 2;
constexpr int WAVE_RADIUS = 2;
constexpr int WAVE_SEGMENT_COUNT = 4;

constexpr Style styleFor(const uint8_t rawStyle) {
  return rawStyle < static_cast<uint8_t>(Style::Count) ? static_cast<Style>(rawStyle) : Style::ShortDash;
}

constexpr int verticalRadius(const uint8_t rawStyle) { return styleFor(rawStyle) == Style::Wavy ? WAVE_RADIUS : 0; }

constexpr bool fitsVertically(const uint8_t rawStyle, const int y, const int top, const int bottomExclusive) {
  const int radius = verticalRadius(rawStyle);
  return y - radius >= top && y + radius < bottomExclusive;
}

struct Pattern {
  int dashLength;
  int gapLength;

  constexpr bool operator==(const Pattern& other) const {
    return dashLength == other.dashLength && gapLength == other.gapLength;
  }
};

constexpr Pattern patternFor(const uint8_t rawStyle) {
  switch (styleFor(rawStyle)) {
    case Style::Solid:
      return {0, 0};
    case Style::ShortDash:
      return {5, 5};
    case Style::MediumDash:
      return {10, 10};
    case Style::LongDash:
      return {20, 10};
    case Style::Dotted:
      return {2, 5};
    case Style::Wavy:
    case Style::Count:
      break;
  }
  return {5, 5};
}

template <typename Renderer>
void draw(Renderer& renderer, int x1, const int y, int x2, const uint8_t rawStyle) {
  if (x2 < x1) std::swap(x1, x2);
  if (styleFor(rawStyle) == Style::Wavy) {
    static constexpr int SLOPES[WAVE_SEGMENT_COUNT] = {-1, 1, 1, -1};
    int x = x1;
    int waveY = y;
    int segment = 0;
    do {
      const int nextX = std::min(x + WAVE_STEP, x2);
      const int nextY = waveY + SLOPES[segment] * (nextX - x);
      renderer.drawLine(x, waveY, nextX, nextY, true);
      x = nextX;
      waveY = nextY;
      segment = (segment + 1) % WAVE_SEGMENT_COUNT;
    } while (x < x2);
    return;
  }

  const Pattern pattern = patternFor(rawStyle);
  if (pattern.gapLength == 0) {
    renderer.drawLine(x1, y, x2, y, true);
    return;
  }

  for (int x = x1; x <= x2; x += pattern.dashLength + pattern.gapLength) {
    renderer.drawLine(x, y, std::min(x + pattern.dashLength - 1, x2), y, true);
  }
}

}  // namespace readingGuideLine
