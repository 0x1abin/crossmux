#pragma once

namespace WeReadQrLayout {

constexpr int kMaxSide = 400;

struct Layout {
  int x;
  int y;
  int side;
  int textY;
};

constexpr int min(const int left, const int right) { return left < right ? left : right; }

constexpr Layout calculate(const int x, const int y, const int width, const int height, const int sidePadding,
                           const int textGap, const int lineHeight, const int sourceSide = kMaxSide) {
  const int widthLimit = width - sidePadding * 2;
  const int footerHeight = textGap + lineHeight * 2;
  const int heightLimit = height - footerHeight * 2;
  const int constrained = min(kMaxSide, min(sourceSide, min(widthLimit, heightLimit)));
  const int side = constrained > 0 ? constrained : 1;
  const int imageX = x + (width - side) / 2;
  const int imageY = y + (height - side) / 2;
  return {imageX, imageY, side, imageY + side + textGap};
}

constexpr Layout kX4Portrait = calculate(0, 80, 480, 700, 16, 8, 20);
static_assert(kX4Portrait.side == 400 && kX4Portrait.x == 40);
static_assert((kX4Portrait.y - 80) * 2 == 700 - kX4Portrait.side);
static_assert(kX4Portrait.textY + 40 <= 780);

constexpr Layout kX3Portrait = calculate(0, 80, 528, 692, 16, 8, 20);
static_assert(kX3Portrait.side == 400 && kX3Portrait.x == 64);
static_assert(kX3Portrait.textY + 40 <= 772);

constexpr Layout kX4Landscape = calculate(0, 80, 800, 380, 16, 8, 20);
static_assert(kX4Landscape.side == 284);
static_assert(kX4Landscape.textY + 40 <= 460);

constexpr Layout kX3Landscape = calculate(0, 80, 792, 428, 16, 8, 20);
static_assert(kX3Landscape.side == 332);
static_assert(kX3Landscape.textY + 40 <= 508);

constexpr Layout kSmallBitmap = calculate(0, 80, 480, 700, 16, 8, 20, 320);
static_assert(kSmallBitmap.side == 320 && kSmallBitmap.x == 80);
static_assert((kSmallBitmap.y - 80) * 2 == 700 - kSmallBitmap.side);

}  // namespace WeReadQrLayout
