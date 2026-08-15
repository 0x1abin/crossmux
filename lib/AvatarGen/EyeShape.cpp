#include "EyeShape.h"

#include "Bezier.h"

namespace avatar {

namespace {

PointF blendedCurvePoint(const PointF& p0, const PointF& p1, const PointF& p2, const PointF& p3,
                         const PointF& leftControl, const PointF& rightControl, const int index) {
  PointF point = cubicBezier(p0, p1, p2, p3, index / 100.0f);
  if (index >= 25) {
    const int i = index - 25;
    const float w = ((75.0f - i) / 75.0f) * ((75.0f - i) / 75.0f);
    const PointF right = cubicBezier(p1, p2, p3, rightControl, i / 100.0f);
    point = PointF{point.x * w + right.x * (1 - w), point.y * w + right.y * (1 - w)};
  }
  if (index < 75) {
    const float w = ((75.0f - index) / 75.0f) * ((75.0f - index) / 75.0f);
    const PointF left = cubicBezier(leftControl, p0, p1, p2, (index + 25) / 100.0f);
    point = PointF{point.x * (1 - w) + left.x * w, point.y * (1 - w) + left.y * w};
  }
  return point;
}

}  // namespace

EyeParams generateEyeParameters(Rng& rng, float width) {
  EyeParams p{};
  p.height_upper = rng.uniform01() * width / 1.2f;
  p.height_lower = rng.uniform01() * width / 1.2f;
  p.P0_upper_randX = rng.uniform01() * 0.4f - 0.2f;
  p.P3_upper_randX = rng.uniform01() * 0.4f - 0.2f;
  p.P0_upper_randY = rng.uniform01() * 0.4f - 0.2f;
  p.P3_upper_randY = rng.uniform01() * 0.4f - 0.2f;
  p.offset_upper_left_randY = rng.uniform01();
  p.offset_upper_right_randY = rng.uniform01();

  PointF P0_upper{-width / 2 + p.P0_upper_randX * width / 16, p.P0_upper_randY * p.height_upper / 16};
  PointF P3_upper{width / 2 + p.P3_upper_randX * width / 16, p.P3_upper_randY * p.height_upper / 16};
  p.eye_true_width = P3_upper.x - P0_upper.x;

  p.offset_upper_left_x = rng.uniform(-p.eye_true_width / 10.0f, p.eye_true_width / 2.3f);
  p.offset_upper_right_x = rng.uniform(-p.eye_true_width / 10.0f, p.eye_true_width / 2.3f);
  p.offset_upper_left_y = p.offset_upper_left_randY * p.height_upper;
  p.offset_upper_right_y = p.offset_upper_right_randY * p.height_upper;
  p.offset_lower_left_x = rng.uniform(p.offset_upper_left_x, p.eye_true_width / 2.1f);
  p.offset_lower_right_x = rng.uniform(p.offset_upper_right_x, p.eye_true_width / 2.1f);
  p.offset_lower_left_y = rng.uniform(-p.offset_upper_left_y + 5.0f, p.height_lower);
  p.offset_lower_right_y = rng.uniform(-p.offset_upper_right_y + 5.0f, p.height_lower);
  p.left_converge0 = rng.uniform01();
  p.right_converge0 = rng.uniform01();
  p.left_converge1 = rng.uniform01();
  p.right_converge1 = rng.uniform01();
  return p;
}

void mutateEyeParameters(Rng& rng, EyeParams& p) {
  float* fields[] = {
      &p.height_upper,         &p.height_lower,        &p.P0_upper_randX,          &p.P3_upper_randX,
      &p.P0_upper_randY,       &p.P3_upper_randY,      &p.offset_upper_left_randY, &p.offset_upper_right_randY,
      &p.eye_true_width,       &p.offset_upper_left_x, &p.offset_upper_right_x,    &p.offset_upper_left_y,
      &p.offset_upper_right_y, &p.offset_lower_left_x, &p.offset_lower_right_x,    &p.offset_lower_left_y,
      &p.offset_lower_right_y, &p.left_converge0,      &p.right_converge0,         &p.left_converge1,
      &p.right_converge1};
  for (float* f : fields) {
    float v = *f;
    *f = v + rng.uniform(-v / 2.0f, v / 2.0f);
  }
}

PointF generateOneEye(Rng& rng, const EyeParams& p, float width, Polyline& outUpper, Polyline& outLower) {
  (void)rng;
  PointF P0_upper{-width / 2 + p.P0_upper_randX * width / 16, p.P0_upper_randY * p.height_upper / 16};
  PointF P3_upper{width / 2 + p.P3_upper_randX * width / 16, p.P3_upper_randY * p.height_upper / 16};
  PointF P0_lower = P0_upper;
  PointF P3_lower = P3_upper;

  PointF P1_upper{P0_upper.x + p.offset_upper_left_x, P0_upper.y + p.offset_upper_left_y};
  PointF P2_upper{P3_upper.x - p.offset_upper_right_x, P3_upper.y + p.offset_upper_right_y};
  PointF P1_lower{P0_lower.x + p.offset_lower_left_x, P0_lower.y - p.offset_lower_left_y};
  PointF P2_lower{P3_lower.x - p.offset_lower_right_x, P3_lower.y - p.offset_lower_right_y};

  PointF upperCtlL{P0_upper.x * (1 - p.left_converge0) + P1_lower.x * p.left_converge0,
                   P0_upper.y * (1 - p.left_converge0) + P1_lower.y * p.left_converge0};
  PointF upperCtlR{P3_upper.x * (1 - p.right_converge0) + P2_lower.x * p.right_converge0,
                   P3_upper.y * (1 - p.right_converge0) + P2_lower.y * p.right_converge0};

  PointF lowerCtlL{P0_lower.x * (1 - p.left_converge0) + P1_upper.x * p.left_converge0,
                   P0_lower.y * (1 - p.left_converge0) + P1_upper.y * p.left_converge0};
  PointF lowerCtlR{P3_lower.x * (1 - p.right_converge1) + P2_upper.x * p.right_converge1,
                   P3_lower.y * (1 - p.right_converge1) + P2_upper.y * p.right_converge1};
  for (int i = 0; i < 100; ++i) {
    outUpper.points[i] = blendedCurvePoint(P0_upper, P1_upper, P2_upper, P3_upper, upperCtlL, upperCtlR, i);
    outLower.points[i] = blendedCurvePoint(P0_lower, P1_lower, P2_lower, P3_lower, lowerCtlL, lowerCtlR, i);
    outUpper.points[i].y = -outUpper.points[i].y;
    outLower.points[i].y = -outLower.points[i].y;
  }

  PointF eyeCenter{outUpper.points[50].x / 2.0f + outLower.points[50].x / 2.0f,
                   outUpper.points[50].y / 2.0f + outLower.points[50].y / 2.0f};
  for (int i = 0; i < 100; ++i) {
    outUpper.points[i].x -= eyeCenter.x;
    outUpper.points[i].y -= eyeCenter.y;
    outLower.points[i].x -= eyeCenter.x;
    outLower.points[i].y -= eyeCenter.y;
  }
  outUpper.count = 100;
  outLower.count = 100;
  outUpper.closed = false;
  outLower.closed = false;
  return PointF{0.0f, 0.0f};
}

void generateBothEyes(Rng& rng, float width, Polyline& outLeftUpper, Polyline& outLeftLower, Polyline& outRightUpper,
                      Polyline& outRightLower, PointF& outLeftCenter, PointF& outRightCenter) {
  EyeParams left = generateEyeParameters(rng, width);
  EyeParams right = left;
  mutateEyeParameters(rng, right);

  outLeftCenter = generateOneEye(rng, left, width, outLeftUpper, outLeftLower);
  outRightCenter = generateOneEye(rng, right, width, outRightUpper, outRightLower);

  for (uint16_t i = 0; i < outLeftUpper.count; ++i) outLeftUpper.points[i].x = -outLeftUpper.points[i].x;
  for (uint16_t i = 0; i < outLeftLower.count; ++i) outLeftLower.points[i].x = -outLeftLower.points[i].x;
}

}  // namespace avatar
