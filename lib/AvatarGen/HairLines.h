#pragma once

#include "AvatarGen.h"

namespace avatar {

uint8_t generateHair(Rng& rng, const Polyline& faceContour, Polyline* outHair, uint8_t maxLines);

}  // namespace avatar
