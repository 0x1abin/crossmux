#pragma once

#include "AvatarGen.h"

namespace avatar {

struct FaceMetrics {
  float width;
  float height;
};

constexpr uint16_t FACE_TMP_BUF_LEN = FACE_QUADRANT_POINTS * 4;

FaceMetrics generateFace(Rng& rng, Polyline& outFace, PointF* tmpBuf0, PointF* tmpBuf1);

}  // namespace avatar
