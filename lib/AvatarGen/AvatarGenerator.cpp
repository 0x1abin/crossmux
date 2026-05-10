#include "AvatarGenerator.h"

#include "EyeShape.h"
#include "HairLines.h"
#include "MouthShape.h"
#include "Nose.h"

namespace avatar {

namespace {

inline void expandBoundsLine(const Polyline& l, float& minX, float& minY, float& maxX, float& maxY) {
  for (uint16_t i = 0; i < l.count; ++i) {
    const PointF& p = l.points[i];
    if (p.x < minX) minX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.x > maxX) maxX = p.x;
    if (p.y > maxY) maxY = p.y;
  }
}

}  // namespace

void AvatarGenerator::generate(uint32_t seed, AvatarData& data) {
  initAvatarData(data);
  Rng rng(seed);

  FaceMetrics fm = generateFace(rng, data.face, tmpFaceBuf0_, tmpFaceBuf1_);

  const float eyeWidth = fm.width / 2.0f;
  generateBothEyes(rng, eyeWidth, data.eyeLeftUpper, data.eyeLeftLower, data.eyeRightUpper, data.eyeRightLower,
                   data.eyeLeftCenter, data.eyeRightCenter);

  const float distBetweenEyes = rng.uniform(fm.width / 4.5f, fm.width / 4.0f);
  const float eyeHeightOffset = rng.uniform(fm.height / 8.0f, fm.height / 6.0f);
  const float leftEyeOffsetX = rng.uniform(-fm.width / 20.0f, fm.width / 10.0f);
  const float leftEyeOffsetY = rng.uniform(-fm.height / 50.0f, fm.height / 50.0f);
  const float rightEyeOffsetX = rng.uniform(-fm.width / 20.0f, fm.width / 10.0f);
  const float rightEyeOffsetY = rng.uniform(-fm.height / 50.0f, fm.height / 50.0f);

  const float leftCx = -(distBetweenEyes + leftEyeOffsetX);
  const float leftCy = -(eyeHeightOffset + leftEyeOffsetY);
  const float rightCx = distBetweenEyes + rightEyeOffsetX;
  const float rightCy = -(eyeHeightOffset + rightEyeOffsetY);
  for (uint16_t i = 0; i < data.eyeLeftUpper.count; ++i) {
    data.eyeLeftUpper.points[i].x += leftCx;
    data.eyeLeftUpper.points[i].y += leftCy;
  }
  for (uint16_t i = 0; i < data.eyeLeftLower.count; ++i) {
    data.eyeLeftLower.points[i].x += leftCx;
    data.eyeLeftLower.points[i].y += leftCy;
  }
  for (uint16_t i = 0; i < data.eyeRightUpper.count; ++i) {
    data.eyeRightUpper.points[i].x += rightCx;
    data.eyeRightUpper.points[i].y += rightCy;
  }
  for (uint16_t i = 0; i < data.eyeRightLower.count; ++i) {
    data.eyeRightLower.points[i].x += rightCx;
    data.eyeRightLower.points[i].y += rightCy;
  }
  data.eyeLeftCenter = PointF{leftCx, leftCy};
  data.eyeRightCenter = PointF{rightCx, rightCy};
  data.pupilRadius = rng.uniform(3.5f, 5.5f);

  generateMouth(rng, fm.width, fm.height, data.mouth);
  data.mouth.thickness = 2;

  data.hairCount = generateHair(rng, data.face, data.hair, MAX_HAIR_LINES);

  generateNose(rng, fm.width, fm.height, data.nose, data.noseStyle, data.noseDotRadius);

  computeBounds(data);
}

void AvatarGenerator::computeBounds(AvatarData& data) {
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  expandBoundsLine(data.face, minX, minY, maxX, maxY);
  expandBoundsLine(data.eyeLeftUpper, minX, minY, maxX, maxY);
  expandBoundsLine(data.eyeLeftLower, minX, minY, maxX, maxY);
  expandBoundsLine(data.eyeRightUpper, minX, minY, maxX, maxY);
  expandBoundsLine(data.eyeRightLower, minX, minY, maxX, maxY);
  expandBoundsLine(data.mouth, minX, minY, maxX, maxY);
  for (uint8_t i = 0; i < data.hairCount; ++i) expandBoundsLine(data.hair[i], minX, minY, maxX, maxY);
  data.minX = minX;
  data.minY = minY;
  data.maxX = maxX;
  data.maxY = maxY;
}

}  // namespace avatar
