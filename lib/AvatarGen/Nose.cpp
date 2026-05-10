#include "Nose.h"

namespace avatar {

void generateNose(Rng& rng, float faceWidth, float faceHeight, Polyline& outNose, uint8_t& outStyle,
                  float& outDotRadius) {
  const float rightX = rng.uniform(faceWidth / 18, faceWidth / 12);
  const float rightY = rng.uniform(0.0f, faceHeight / 5);
  const float leftX = -rng.uniform(faceWidth / 18, faceWidth / 12);
  const float leftY = rightY + rng.uniform(-faceHeight / 30, faceHeight / 20);

  if (rng.coin(0.5f)) {
    outStyle = 0;
    outDotRadius = rng.uniform(1.5f, 2.5f);
    if (outNose.capacity >= 2) {
      outNose.points[0] = PointF{leftX, leftY};
      outNose.points[1] = PointF{rightX, rightY};
      outNose.count = 2;
    } else {
      outNose.count = 0;
    }
    outNose.closed = false;
  } else {
    outStyle = 1;
    outDotRadius = 0.0f;
    const PointF p0{leftX, leftY};
    const PointF p2{(leftX + rightX) / 2.0f, -faceHeight / 5.0f * 0.2f};
    const PointF q{rightX, rightY * 1.5f};
    const uint8_t samples = outNose.capacity > MAX_NOSE_POINTS ? MAX_NOSE_POINTS : outNose.capacity;
    for (uint8_t i = 0; i < samples; ++i) {
      const float t = static_cast<float>(i) / (samples - 1);
      const float u = 1.0f - t;
      outNose.points[i] =
          PointF{u * u * p0.x + 2 * u * t * q.x + t * t * p2.x, u * u * p0.y + 2 * u * t * q.y + t * t * p2.y};
    }
    outNose.count = samples;
    outNose.closed = false;
    outNose.thickness = 2;
  }
}

}  // namespace avatar
