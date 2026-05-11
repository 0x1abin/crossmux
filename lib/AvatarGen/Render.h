#pragma once

#include <GfxRenderer.h>

#include "AvatarGen.h"

namespace avatar {

struct ScreenRect {
  int x;
  int y;
  int width;
  int height;
};

struct Transform {
  float scale;
  int offsetX;
  int offsetY;
};

Transform computeFitTransform(const AvatarData& data, const ScreenRect& viewport);

void drawAvatar(const GfxRenderer& renderer, const AvatarData& data, const ScreenRect& viewport);

}  // namespace avatar
