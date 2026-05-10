#include "Bezier.h"

namespace avatar {

PointF generalBezier(const PointF* controls, uint8_t order, float t) {
  PointF tmp[MAX_HAIR_CTRL_POINTS + 1];
  for (uint8_t i = 0; i <= order; ++i) tmp[i] = controls[i];
  for (uint8_t r = 1; r <= order; ++r) {
    for (uint8_t i = 0; i <= order - r; ++i) {
      tmp[i] = lerp(tmp[i], tmp[i + 1], t);
    }
  }
  return tmp[0];
}

}  // namespace avatar
