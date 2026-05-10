#pragma once

#include <cmath>
#include <cstdint>

namespace avatar {

struct Point2D {
  int16_t x;
  int16_t y;
};

struct PointF {
  float x;
  float y;
};

inline PointF makePoint(float x, float y) { return PointF{x, y}; }

class Rng {
 public:
  explicit Rng(uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}

  uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  float uniform01() { return static_cast<float>(next() & 0x00FFFFFFu) / static_cast<float>(0x01000000u); }

  float uniform(float min, float max) { return min + uniform01() * (max - min); }

  bool coin(float pTrue) { return uniform01() < pTrue; }

  int rangeInt(int min, int maxExclusive) {
    if (maxExclusive <= min) return min;
    return min + static_cast<int>(next() % static_cast<uint32_t>(maxExclusive - min));
  }

 private:
  uint32_t state_;
};

constexpr uint16_t FACE_QUADRANT_POINTS = 50;
constexpr uint16_t MAX_FACE_POINTS = FACE_QUADRANT_POINTS * 4 + 4;
constexpr uint16_t MAX_EYELID_POINTS = 100;
constexpr uint16_t MAX_MOUTH_POINTS = 80;
constexpr uint8_t MAX_HAIR_LINES = 24;
constexpr uint16_t MAX_HAIR_POINTS_PER_LINE = 32;
constexpr uint8_t MAX_HAIR_CTRL_POINTS = 28;
constexpr uint8_t MAX_NOSE_POINTS = 12;

struct Polyline {
  PointF* points;
  uint16_t count;
  uint16_t capacity;
  uint8_t thickness;
  bool closed;
};

struct AvatarData {
  PointF faceBuf[MAX_FACE_POINTS];
  Polyline face;

  PointF eyeLeftUpperBuf[MAX_EYELID_POINTS];
  PointF eyeLeftLowerBuf[MAX_EYELID_POINTS];
  PointF eyeRightUpperBuf[MAX_EYELID_POINTS];
  PointF eyeRightLowerBuf[MAX_EYELID_POINTS];
  Polyline eyeLeftUpper;
  Polyline eyeLeftLower;
  Polyline eyeRightUpper;
  Polyline eyeRightLower;

  PointF eyeLeftCenter;
  PointF eyeRightCenter;
  float pupilRadius;

  PointF mouthBuf[MAX_MOUTH_POINTS];
  Polyline mouth;

  PointF hairBuf[MAX_HAIR_LINES][MAX_HAIR_POINTS_PER_LINE];
  Polyline hair[MAX_HAIR_LINES];
  uint8_t hairCount;

  PointF noseBuf[MAX_NOSE_POINTS];
  Polyline nose;
  uint8_t noseStyle;
  float noseDotRadius;

  float minX, minY, maxX, maxY;
};

void initAvatarData(AvatarData& data);

}  // namespace avatar
