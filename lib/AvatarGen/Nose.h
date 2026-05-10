#pragma once

#include "AvatarGen.h"

namespace avatar {

void generateNose(Rng& rng, float faceWidth, float faceHeight, Polyline& outNose, uint8_t& outStyle,
                  float& outDotRadius);

}  // namespace avatar
