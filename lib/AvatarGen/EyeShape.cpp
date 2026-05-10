#include "EyeShape.h"

#include "Bezier.h"

namespace avatar {

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

  PointF upperCurve[100];
  PointF upperCtlLCurve[100];
  PointF upperCtlRCurve[100];
  for (int t = 0; t < 100; ++t) {
    const float u = t / 100.0f;
    upperCurve[t] = cubicBezier(P0_upper, P1_upper, P2_upper, P3_upper, u);
    upperCtlLCurve[t] = cubicBezier(upperCtlL, P0_upper, P1_upper, P2_upper, u);
    upperCtlRCurve[t] = cubicBezier(P1_upper, P2_upper, P3_upper, upperCtlR, u);
  }
  for (int i = 0; i < 75; ++i) {
    const float w = ((75.0f - i) / 75.0f) * ((75.0f - i) / 75.0f);
    upperCurve[i] = PointF{upperCurve[i].x * (1 - w) + upperCtlLCurve[i + 25].x * w,
                           upperCurve[i].y * (1 - w) + upperCtlLCurve[i + 25].y * w};
    upperCurve[i + 25] = PointF{upperCurve[i + 25].x * w + upperCtlRCurve[i].x * (1 - w),
                                upperCurve[i + 25].y * w + upperCtlRCurve[i].y * (1 - w)};
  }

  PointF lowerCurve[100];
  PointF lowerCtlLCurve[100];
  PointF lowerCtlRCurve[100];
  PointF lowerCtlL{P0_lower.x * (1 - p.left_converge0) + P1_upper.x * p.left_converge0,
                   P0_lower.y * (1 - p.left_converge0) + P1_upper.y * p.left_converge0};
  PointF lowerCtlR{P3_lower.x * (1 - p.right_converge1) + P2_upper.x * p.right_converge1,
                   P3_lower.y * (1 - p.right_converge1) + P2_upper.y * p.right_converge1};
  for (int t = 0; t < 100; ++t) {
    const float u = t / 100.0f;
    lowerCurve[t] = cubicBezier(P0_lower, P1_lower, P2_lower, P3_lower, u);
    lowerCtlLCurve[t] = cubicBezier(lowerCtlL, P0_lower, P1_lower, P2_lower, u);
    lowerCtlRCurve[t] = cubicBezier(P1_lower, P2_lower, P3_lower, lowerCtlR, u);
  }
  for (int i = 0; i < 75; ++i) {
    const float w = ((75.0f - i) / 75.0f) * ((75.0f - i) / 75.0f);
    lowerCurve[i] = PointF{lowerCurve[i].x * (1 - w) + lowerCtlLCurve[i + 25].x * w,
                           lowerCurve[i].y * (1 - w) + lowerCtlLCurve[i + 25].y * w};
    lowerCurve[i + 25] = PointF{lowerCurve[i + 25].x * w + lowerCtlRCurve[i].x * (1 - w),
                                lowerCurve[i + 25].y * w + lowerCtlRCurve[i].y * (1 - w)};
  }

  for (int i = 0; i < 100; ++i) {
    upperCurve[i].y = -upperCurve[i].y;
    lowerCurve[i].y = -lowerCurve[i].y;
  }

  PointF eyeCenter{upperCurve[50].x / 2.0f + lowerCurve[50].x / 2.0f,
                   upperCurve[50].y / 2.0f + lowerCurve[50].y / 2.0f};
  for (int i = 0; i < 100; ++i) {
    upperCurve[i].x -= eyeCenter.x;
    upperCurve[i].y -= eyeCenter.y;
    lowerCurve[i].x -= eyeCenter.x;
    lowerCurve[i].y -= eyeCenter.y;
  }

  for (int i = 0; i < 100; ++i) {
    outUpper.points[i] = upperCurve[i];
    outLower.points[i] = lowerCurve[i];
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
