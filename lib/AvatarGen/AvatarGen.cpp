#include "AvatarGen.h"

namespace avatar {

void initAvatarData(AvatarData& data) {
  data.face = Polyline{data.faceBuf, 0, MAX_FACE_POINTS, 1, true};
  data.eyeLeftUpper = Polyline{data.eyeLeftUpperBuf, 0, MAX_EYELID_POINTS, 1, false};
  data.eyeLeftLower = Polyline{data.eyeLeftLowerBuf, 0, MAX_EYELID_POINTS, 1, false};
  data.eyeRightUpper = Polyline{data.eyeRightUpperBuf, 0, MAX_EYELID_POINTS, 1, false};
  data.eyeRightLower = Polyline{data.eyeRightLowerBuf, 0, MAX_EYELID_POINTS, 1, false};
  data.eyeLeftCenter = PointF{0, 0};
  data.eyeRightCenter = PointF{0, 0};
  data.pupilRadius = 4.0f;
  data.mouth = Polyline{data.mouthBuf, 0, MAX_MOUTH_POINTS, 2, false};
  for (uint8_t i = 0; i < MAX_HAIR_LINES; ++i) {
    data.hair[i] = Polyline{data.hairBuf[i], 0, MAX_HAIR_POINTS_PER_LINE, 1, false};
  }
  data.hairCount = 0;
  data.nose = Polyline{data.noseBuf, 0, MAX_NOSE_POINTS, 1, false};
  data.noseStyle = 0;
  data.noseDotRadius = 2.0f;
  data.minX = data.minY = data.maxX = data.maxY = 0.0f;
}

}  // namespace avatar
