#pragma once

#include "AvatarGen.h"
#include "FaceShape.h"

namespace avatar {

class AvatarGenerator {
 public:
  AvatarGenerator() = default;

  void generate(uint32_t seed, AvatarData& outData);

 private:
  PointF tmpFaceBuf0_[FACE_TMP_BUF_LEN];
  PointF tmpFaceBuf1_[FACE_TMP_BUF_LEN];

  void computeBounds(AvatarData& data);
};

}  // namespace avatar
