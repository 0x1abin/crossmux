#include "FaceShape.h"

#include <cmath>

namespace avatar {

namespace {

void getEggShapePoints(Rng& rng, float a, float b, float k, uint16_t segPoints, PointF* out) {
  const float jitterAng = static_cast<float>(M_PI) / 1.1f / static_cast<float>(segPoints);
  const float jitterX = a / 200.0f;
  uint16_t idx = 0;
  for (uint16_t i = 0; i < segPoints; ++i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    float y = sinf(deg) * b;
    float radicand = (1.0f - (y * y) / (b * b)) / (1.0f + k * y) * (a * a);
    if (radicand < 0.0f) radicand = 0.0f;
    float x = sqrtf(radicand) + rng.uniform(-jitterX, jitterX);
    out[idx++] = PointF{x, y};
  }
  for (uint16_t i = segPoints; i > 0; --i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    float y = sinf(deg) * b;
    float radicand = (1.0f - (y * y) / (b * b)) / (1.0f + k * y) * (a * a);
    if (radicand < 0.0f) radicand = 0.0f;
    float x = -sqrtf(radicand) + rng.uniform(-jitterX, jitterX);
    out[idx++] = PointF{x, y};
  }
  for (uint16_t i = 0; i < segPoints; ++i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    float y = -sinf(deg) * b;
    float radicand = (1.0f - (y * y) / (b * b)) / (1.0f + k * y) * (a * a);
    if (radicand < 0.0f) radicand = 0.0f;
    float x = -sqrtf(radicand) + rng.uniform(-jitterX, jitterX);
    out[idx++] = PointF{x, y};
  }
  for (uint16_t i = segPoints; i > 0; --i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    float y = -sinf(deg) * b;
    float radicand = (1.0f - (y * y) / (b * b)) / (1.0f + k * y) * (a * a);
    if (radicand < 0.0f) radicand = 0.0f;
    float x = sqrtf(radicand) + rng.uniform(-jitterX, jitterX);
    out[idx++] = PointF{x, y};
  }
}

PointF intersectionPoint(float radian, float a, float b) {
  if (radian < 0.0f) radian = 0.0f;
  if (radian > static_cast<float>(M_PI_2)) radian = static_cast<float>(M_PI_2);
  if (fabsf(radian - static_cast<float>(M_PI_2)) < 0.0001f) return PointF{0.0f, b};
  const float m = tanf(radian);
  const float y = m * a;
  if (y < b) return PointF{a, y};
  return PointF{b / m, b};
}

void getRectShapePoints(Rng& rng, float a, float b, uint16_t segPoints, PointF* out) {
  const float jitterAng = static_cast<float>(M_PI) / 11.0f / static_cast<float>(segPoints);
  uint16_t idx = 0;
  for (uint16_t i = 0; i < segPoints; ++i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    PointF p = intersectionPoint(deg, a, b);
    out[idx++] = PointF{p.x, p.y};
  }
  for (uint16_t i = segPoints; i > 0; --i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    PointF p = intersectionPoint(deg, a, b);
    out[idx++] = PointF{-p.x, p.y};
  }
  for (uint16_t i = 0; i < segPoints; ++i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    PointF p = intersectionPoint(deg, a, b);
    out[idx++] = PointF{-p.x, -p.y};
  }
  for (uint16_t i = segPoints; i > 0; --i) {
    float deg = (static_cast<float>(M_PI_2) / segPoints) * i + rng.uniform(-jitterAng, jitterAng);
    PointF p = intersectionPoint(deg, a, b);
    out[idx++] = PointF{p.x, -p.y};
  }
}

}  // namespace

FaceMetrics generateFace(Rng& rng, Polyline& outFace, PointF* buf0, PointF* buf1) {
  const uint16_t seg = FACE_QUADRANT_POINTS;
  const uint16_t total = seg * 4;

  const float faceSizeX0 = rng.uniform(50.0f, 100.0f);
  const float faceSizeY0 = rng.uniform(70.0f, 100.0f);
  const float faceSizeY1 = rng.uniform(50.0f, 80.0f);
  const float faceSizeX1 = rng.uniform(70.0f, 100.0f);
  const float faceK0 = rng.uniform(0.001f, 0.005f) * (rng.coin(0.5f) ? 1.0f : -1.0f);
  const float faceK1 = rng.uniform(0.001f, 0.005f) * (rng.coin(0.5f) ? 1.0f : -1.0f);
  const float face0Tx = rng.uniform(-5.0f, 5.0f);
  const float face0Ty = rng.uniform(-15.0f, 15.0f);
  const float face1Tx = rng.uniform(-5.0f, 25.0f);
  const float face1Ty = rng.uniform(-5.0f, 5.0f);
  const bool eggOrRect0 = rng.coin(0.9f);
  const bool eggOrRect1 = rng.coin(0.7f);

  if (eggOrRect0) {
    getEggShapePoints(rng, faceSizeX0, faceSizeY0, faceK0, seg, buf0);
  } else {
    getRectShapePoints(rng, faceSizeX0, faceSizeY0, seg, buf0);
  }
  if (eggOrRect1) {
    getEggShapePoints(rng, faceSizeX1, faceSizeY1, faceK1, seg, buf1);
  } else {
    getRectShapePoints(rng, faceSizeX1, faceSizeY1, seg, buf1);
  }

  for (uint16_t i = 0; i < total; ++i) {
    buf0[i].x += face0Tx;
    buf0[i].y += face0Ty;
    buf1[i].x += face1Tx;
    buf1[i].y += face1Ty;
  }

  PointF center{0.0f, 0.0f};
  for (uint16_t i = 0; i < total; ++i) {
    const uint16_t j = (i + total / 4) % total;
    PointF p{buf0[i].x * 0.7f + buf1[j].y * 0.3f, buf0[i].y * 0.7f - buf1[j].x * 0.3f};
    outFace.points[i] = p;
    center.x += p.x;
    center.y += p.y;
  }
  center.x /= total;
  center.y /= total;
  for (uint16_t i = 0; i < total; ++i) {
    outFace.points[i].x -= center.x;
    outFace.points[i].y -= center.y;
  }

  outFace.count = total;
  outFace.closed = true;

  const float width = outFace.points[0].x - outFace.points[total / 2].x;
  const float height = outFace.points[total / 4].y - outFace.points[(total * 3) / 4].y;
  return FaceMetrics{fabsf(width), fabsf(height)};
}

}  // namespace avatar
