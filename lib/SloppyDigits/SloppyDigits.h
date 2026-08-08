#pragma once

#include <cstdint>

class GfxRenderer;

namespace sloppy {

struct Bounds {
  int x;
  int y;
  int width;
  int height;
};

enum class AlphabetId : uint8_t {
  Geometric = 0,
  Script,
  Marker,
  Count,
};

struct Style {
  AlphabetId alphabet;
  float wobble;          // Control-point jitter in 100x160 glyph units.
  uint8_t strokeWidth;   // Physical pixels; intentionally independent of scale.
  float slantDeg;        // Horizontal shear angle.
  float digitRotateMax;  // Maximum seeded rotation per digit.
  uint8_t digitGap;      // Gap in glyph units.
  bool oneIsPlain;       // Draw 1 without its hook and base.
};

struct PointSeed {
  int8_t dx;
  int8_t dy;
};

struct PositionSeed {
  int8_t rotJitter;
  int8_t sxJitter;
  int8_t syJitter;
};

constexpr uint8_t kMaxGlyphControlPoints = 20;
constexpr int kMaxRows = 4;
constexpr int kMaxDigitsPerRow = 10;
constexpr int kMaxDigitPositions = 10;

struct Seeds {
  PointSeed glyphSeeds[10][kMaxGlyphControlPoints];
  uint8_t glyphSeedCount[10];
  PositionSeed positions[kMaxDigitPositions];
};

static_assert(sizeof(Seeds) == 440);

void rollStyle(uint32_t seed, Style& out);
void prepareSeeds(uint32_t seed, const Style& style, Seeds& out);

// Newlines split rows; other non-digit characters are ignored.
void draw(GfxRenderer& renderer, const Style& style, const Seeds& seeds, const char* digits, Bounds bounds);

}  // namespace sloppy
