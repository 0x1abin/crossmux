#include "MouthShape.h"

#include <cmath>

#include "Bezier.h"

namespace avatar {

namespace {

constexpr int MOUTH_SAMPLES_PER_CURVE = 25;
constexpr float MOUTH_STEP = 1.0f / static_cast<float>(MOUTH_SAMPLES_PER_CURVE);

void mouthShape0Or1(Rng& rng, float faceWidth, float faceHeight, bool variant1, Polyline& outMouth) {
  const float divUpper = variant1 ? 4.0f : 3.5f;
  const float mouthRightY = rng.uniform(faceHeight / 7, faceHeight / divUpper);
  const float mouthLeftY = rng.uniform(faceHeight / 7, faceHeight / divUpper);
  const float mouthRightX = rng.uniform(faceWidth / 10, faceWidth / 2);
  const float mouthLeftX = -mouthRightX + rng.uniform(-faceWidth / 20, faceWidth / 20);
  const PointF mouthRight{mouthRightX, mouthRightY};
  const PointF mouthLeft{mouthLeftX, mouthLeftY};
  const PointF ctl0{rng.uniform(0.0f, mouthRightX), rng.uniform(mouthLeftY + 5.0f, faceHeight / 1.5f)};
  const PointF ctl1{rng.uniform(mouthLeftX, 0.0f), rng.uniform(mouthLeftY + 5.0f, faceHeight / 1.5f)};

  uint16_t idx = 0;
  for (int i = 0; i < MOUTH_SAMPLES_PER_CURVE && idx < outMouth.capacity; ++i) {
    outMouth.points[idx++] = cubicBezier(mouthLeft, ctl1, ctl0, mouthRight, i * MOUTH_STEP);
  }
  if (rng.coin(0.5f)) {
    for (int i = 0; i < MOUTH_SAMPLES_PER_CURVE && idx < outMouth.capacity; ++i) {
      outMouth.points[idx++] = cubicBezier(mouthRight, ctl0, ctl1, mouthLeft, i * MOUTH_STEP);
    }
  } else {
    const float yPortion = rng.uniform(0.0f, 0.8f);
    const PointF first = outMouth.points[0];
    const PointF last = outMouth.points[MOUTH_SAMPLES_PER_CURVE - 1];
    const int n = MOUTH_SAMPLES_PER_CURVE;
    for (int i = 0; i < n && idx < outMouth.capacity; ++i) {
      const float t = static_cast<float>(i) / n;
      const float yMirror = outMouth.points[(n - 1) - i].y;
      outMouth.points[idx++] = PointF{last.x * (1 - t) + first.x * t,
                                      (last.y * (1 - t) + first.y * t) * (1 - yPortion) + yMirror * yPortion};
    }
  }
  outMouth.count = idx;
  outMouth.closed = true;

  if (variant1 && idx > 0) {
    PointF center{(mouthRight.x + mouthLeft.x) / 2.0f, outMouth.points[MOUTH_SAMPLES_PER_CURVE / 4].y / 2.0f +
                                                           outMouth.points[(3 * MOUTH_SAMPLES_PER_CURVE) / 4].y / 2.0f};
    for (uint16_t i = 0; i < idx; ++i) {
      PointF& p = outMouth.points[i];
      p.x -= center.x;
      p.y -= center.y;
      p.y = -p.y;
      p.x *= 0.6f;
      p.y *= 0.6f;
      p.x += center.x;
      p.y += center.y * 0.8f;
    }
  }
}

void mouthShape2(Rng& rng, float faceWidth, float faceHeight, Polyline& outMouth) {
  const PointF center{rng.uniform(-faceWidth / 8, faceWidth / 8), rng.uniform(faceHeight / 4, faceHeight / 2.5f)};
  const float a = rng.uniform(faceWidth / 10, faceWidth / 4);
  const float b = rng.uniform(faceHeight / 20, faceHeight / 10);
  constexpr int seg = 12;
  const float jitterAng = static_cast<float>(M_PI) / 1.1f / static_cast<float>(seg);
  uint16_t idx = 0;
  auto pushQuadrant = [&](int signX, int signY, bool reverse) {
    for (int n = 0; n < seg; ++n) {
      const int i = reverse ? seg - n : n;
      if (i == 0 || i >= seg) continue;
      const float deg = (static_cast<float>(M_PI_2) / seg) * i + rng.uniform(-jitterAng, jitterAng);
      const float y = signY * sinf(deg) * b;
      const float radicand = (1.0f - (y * y) / (b * b)) * a * a;
      const float x = signX * sqrtf(radicand > 0 ? radicand : 0);
      if (idx >= outMouth.capacity) return;
      outMouth.points[idx++] = PointF{x, y};
    }
  };
  pushQuadrant(+1, +1, false);
  pushQuadrant(-1, +1, true);
  pushQuadrant(-1, -1, false);
  pushQuadrant(+1, -1, true);

  const float rot = rng.uniform(-static_cast<float>(M_PI) / 9.5f, static_cast<float>(M_PI) / 9.5f);
  const float c = cosf(rot);
  const float s = sinf(rot);
  for (uint16_t i = 0; i < idx; ++i) {
    PointF p = outMouth.points[i];
    outMouth.points[i] = PointF{p.x * c - p.y * s + center.x, p.x * s + p.y * c + center.y};
  }
  outMouth.count = idx;
  outMouth.closed = true;
}

}  // namespace

void generateMouth(Rng& rng, float faceWidth, float faceHeight, Polyline& outMouth) {
  const int choice = rng.rangeInt(0, 3);
  if (choice == 0) {
    mouthShape0Or1(rng, faceWidth, faceHeight, false, outMouth);
  } else if (choice == 1) {
    mouthShape0Or1(rng, faceWidth, faceHeight, true, outMouth);
  } else {
    mouthShape2(rng, faceWidth, faceHeight, outMouth);
  }
}

}  // namespace avatar
