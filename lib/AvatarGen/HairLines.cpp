#include "HairLines.h"

#include <cmath>

#include "Bezier.h"

namespace avatar {

namespace {

inline uint16_t wrapIndex(int i, uint16_t total) {
  int m = i % static_cast<int>(total);
  if (m < 0) m += total;
  return static_cast<uint16_t>(m);
}

uint8_t method0(Rng& rng, const Polyline& face, Polyline* outHair, uint8_t maxLines, uint8_t numLines) {
  uint8_t produced = 0;
  for (uint8_t i = 0; i < numLines && produced < maxLines; ++i) {
    const uint16_t numHairPoints = static_cast<uint16_t>(20 + rng.rangeInt(-5, 6));
    const uint16_t ctrlCount = numHairPoints > MAX_HAIR_CTRL_POINTS ? MAX_HAIR_CTRL_POINTS : numHairPoints;
    PointF d0[MAX_HAIR_POINTS_PER_LINE];
    PointF d1[MAX_HAIR_POINTS_PER_LINE];
    PointF ctrl[MAX_HAIR_CTRL_POINTS];

    int idxOff0 = rng.rangeInt(15, 70);
    for (uint16_t j = 0; j < ctrlCount; ++j) {
      ctrl[j] = face.points[wrapIndex(static_cast<int>(face.count) - (j + idxOff0), face.count)];
    }
    for (uint16_t j = 0; j < numHairPoints; ++j) {
      d0[j] = generalBezier(ctrl, static_cast<uint8_t>(ctrlCount - 1), j / static_cast<float>(numHairPoints));
    }

    int idxOff1 = rng.rangeInt(15, 70);
    for (uint16_t j = 0; j < ctrlCount; ++j) {
      ctrl[j] = face.points[wrapIndex(static_cast<int>(face.count) - (-static_cast<int>(j) + idxOff1), face.count)];
    }
    for (uint16_t j = 0; j < numHairPoints; ++j) {
      d1[j] = generalBezier(ctrl, static_cast<uint8_t>(ctrlCount - 1), j / static_cast<float>(numHairPoints));
    }

    Polyline& line = outHair[produced];
    for (uint16_t j = 0; j < numHairPoints && j < line.capacity; ++j) {
      const float w = (j / static_cast<float>(numHairPoints));
      const float w2 = w * w;
      line.points[j] = PointF{d0[j].x * w2 + d1[j].x * (1 - w2), d0[j].y * w2 + d1[j].y * (1 - w2)};
    }
    line.count = numHairPoints > line.capacity ? line.capacity : numHairPoints;
    line.closed = false;
    line.thickness = 1;
    ++produced;
  }
  return produced;
}

uint8_t method2(Rng& rng, const Polyline& face, Polyline* outHair, uint8_t maxLines, uint8_t numLines) {
  uint8_t produced = 0;
  for (uint8_t i = 0; i < numLines && produced < maxLines; ++i) {
    const uint16_t numHairPoints = static_cast<uint16_t>(20 + rng.rangeInt(-5, 6));
    const uint16_t ctrlCount = numHairPoints > MAX_HAIR_CTRL_POINTS ? MAX_HAIR_CTRL_POINTS : numHairPoints;
    PointF ctrl[MAX_HAIR_CTRL_POINTS];

    const int idxOffset = rng.rangeInt(5, 90);
    const float lower = rng.uniform(0.8f, 1.4f);
    const int reverse = rng.coin(0.5f) ? 1 : -1;
    for (uint16_t j = 0; j < ctrlCount; ++j) {
      const float power = rng.uniform(0.1f, 3.0f);
      const float t = static_cast<float>(j) / numHairPoints;
      const float portion = (1.0f - powf(t, power)) * (1.0f - lower) + lower;
      const PointF& src = face.points[wrapIndex(
          static_cast<int>(face.count) - (reverse * static_cast<int>(j) + idxOffset), face.count)];
      ctrl[j] = PointF{src.x * portion, src.y * portion};
    }

    Polyline& line = outHair[produced];
    for (uint16_t j = 0; j < numHairPoints && j < line.capacity; ++j) {
      line.points[j] = generalBezier(ctrl, static_cast<uint8_t>(ctrlCount - 1), j / static_cast<float>(numHairPoints));
    }
    line.count = numHairPoints > line.capacity ? line.capacity : numHairPoints;
    line.closed = false;
    line.thickness = 1;
    ++produced;
  }
  return produced;
}

}  // namespace

uint8_t generateHair(Rng& rng, const Polyline& faceContour, Polyline* outHair, uint8_t maxLines) {
  uint8_t total = 0;
  if (rng.coin(0.7f) && total < maxLines) {
    const uint8_t n = static_cast<uint8_t>(rng.rangeInt(0, 8) + 4);
    total += method0(rng, faceContour, outHair + total, static_cast<uint8_t>(maxLines - total), n);
  }
  if (rng.coin(0.7f) && total < maxLines) {
    const uint8_t n = static_cast<uint8_t>(rng.rangeInt(0, 8) + 4);
    total += method2(rng, faceContour, outHair + total, static_cast<uint8_t>(maxLines - total), n);
  }
  if (rng.coin(0.5f) && total < maxLines) {
    const uint8_t n = static_cast<uint8_t>(rng.rangeInt(0, 6) + 4);
    total += method2(rng, faceContour, outHair + total, static_cast<uint8_t>(maxLines - total), n);
  }
  return total;
}

}  // namespace avatar
