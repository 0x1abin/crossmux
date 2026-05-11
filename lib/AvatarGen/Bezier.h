#pragma once

#include "AvatarGen.h"

namespace avatar {

inline PointF cubicBezier(const PointF& p0, const PointF& p1, const PointF& p2, const PointF& p3, float t) {
  const float u = 1.0f - t;
  const float uu = u * u;
  const float uuu = uu * u;
  const float tt = t * t;
  const float ttt = tt * t;
  return PointF{uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
                uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y};
}

inline PointF lerp(const PointF& a, const PointF& b, float w) {
  return PointF{a.x * (1.0f - w) + b.x * w, a.y * (1.0f - w) + b.y * w};
}

PointF generalBezier(const PointF* controls, uint8_t order, float t);

}  // namespace avatar
