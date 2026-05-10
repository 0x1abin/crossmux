#pragma once

#include "AvatarGen.h"

namespace avatar {

struct EyeParams {
  float height_upper, height_lower;
  float P0_upper_randX, P3_upper_randX, P0_upper_randY, P3_upper_randY;
  float offset_upper_left_randY, offset_upper_right_randY;
  float eye_true_width;
  float offset_upper_left_x, offset_upper_right_x, offset_upper_left_y, offset_upper_right_y;
  float offset_lower_left_x, offset_lower_right_x, offset_lower_left_y, offset_lower_right_y;
  float left_converge0, right_converge0, left_converge1, right_converge1;
};

EyeParams generateEyeParameters(Rng& rng, float width);
void mutateEyeParameters(Rng& rng, EyeParams& p);
PointF generateOneEye(Rng& rng, const EyeParams& p, float width, Polyline& outUpper, Polyline& outLower);

void generateBothEyes(Rng& rng, float width, Polyline& outLeftUpper, Polyline& outLeftLower, Polyline& outRightUpper,
                      Polyline& outRightLower, PointF& outLeftCenter, PointF& outRightCenter);

}  // namespace avatar
