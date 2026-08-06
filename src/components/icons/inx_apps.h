#pragma once

#include <GfxRenderer.h>

#include <array>
#include <cstdint>

#include "components/themes/BaseTheme.h"

namespace InxAppIcons {
inline constexpr int size = 32;
inline constexpr int bytes = size * size / 8;

struct Bitmap {
  std::array<uint8_t, bytes> data{};

  constexpr Bitmap() { data.fill(0xff); }

  constexpr void ink(const int x, const int y) {
    if (x < 0 || x >= size || y < 0 || y >= size) return;
    data[y * (size / 8) + x / 8] &= static_cast<uint8_t>(~(1U << (7 - x % 8)));
  }

  constexpr void line(int x0, int y0, const int x1, const int y1) {
    const int dx = x0 < x1 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = y0 < y1 ? y0 - y1 : y1 - y0;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
      ink(x0, y0);
      if (x0 == x1 && y0 == y1) break;
      const int twice = error * 2;
      if (twice >= dy) {
        error += dy;
        x0 += sx;
      }
      if (twice <= dx) {
        error += dx;
        y0 += sy;
      }
    }
  }

  constexpr void rect(const int x, const int y, const int width, const int height) {
    line(x, y, x + width - 1, y);
    line(x, y + height - 1, x + width - 1, y + height - 1);
    line(x, y, x, y + height - 1);
    line(x + width - 1, y, x + width - 1, y + height - 1);
  }

  constexpr void filledRect(const int x, const int y, const int width, const int height) {
    for (int row = y; row < y + height; ++row) line(x, row, x + width - 1, row);
  }

  constexpr void circle(const int cx, const int cy, const int radius) {
    const int outer = radius * radius;
    const int inner = (radius - 1) * (radius - 1);
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        const int distance = x * x + y * y;
        if (distance <= outer && distance >= inner) ink(cx + x, cy + y);
      }
    }
  }
};

constexpr Bitmap transfer() {
  Bitmap icon;
  icon.rect(8, 3, 16, 26);
  icon.line(4, 10, 17, 10);
  icon.line(4, 10, 8, 6);
  icon.line(4, 10, 8, 14);
  icon.line(28, 22, 15, 22);
  icon.line(28, 22, 24, 18);
  icon.line(28, 22, 24, 26);
  return icon;
}

constexpr Bitmap opds() {
  Bitmap icon;
  icon.line(3, 10, 15, 13);
  icon.line(15, 13, 15, 28);
  icon.line(15, 28, 3, 25);
  icon.line(3, 10, 3, 25);
  icon.line(17, 13, 29, 10);
  icon.line(17, 13, 17, 28);
  icon.line(17, 28, 29, 25);
  icon.line(29, 10, 29, 25);
  icon.circle(16, 5, 2);
  icon.line(11, 5, 12, 2);
  icon.line(21, 5, 20, 2);
  return icon;
}

constexpr Bitmap readingStats() {
  Bitmap icon;
  icon.line(4, 28, 28, 28);
  icon.rect(6, 18, 4, 10);
  icon.rect(14, 12, 4, 16);
  icon.rect(22, 6, 4, 22);
  icon.circle(8, 8, 5);
  icon.line(8, 8, 8, 5);
  icon.line(8, 8, 11, 9);
  return icon;
}

constexpr Bitmap readingHeatmap() {
  Bitmap icon;
  icon.rect(4, 5, 24, 23);
  for (int x = 10; x <= 22; x += 6) icon.line(x, 5, x, 27);
  for (int y = 11; y <= 23; y += 6) icon.line(4, y, 27, y);
  icon.filledRect(5, 18, 5, 5);
  icon.filledRect(17, 6, 5, 5);
  icon.filledRect(23, 12, 4, 5);
  return icon;
}

constexpr Bitmap readingProfile() {
  Bitmap icon;
  icon.circle(16, 8, 4);
  icon.line(8, 27, 9, 20);
  icon.line(9, 20, 13, 16);
  icon.line(13, 16, 19, 16);
  icon.line(19, 16, 23, 20);
  icon.line(23, 20, 24, 27);
  icon.line(8, 27, 24, 27);
  icon.line(16, 18, 16, 25);
  icon.line(12, 22, 20, 22);
  return icon;
}

constexpr Bitmap achievements() {
  Bitmap icon;
  icon.rect(10, 4, 12, 10);
  icon.line(10, 7, 5, 7);
  icon.line(5, 7, 5, 12);
  icon.line(5, 12, 10, 16);
  icon.line(22, 7, 27, 7);
  icon.line(27, 7, 27, 12);
  icon.line(27, 12, 22, 16);
  icon.line(13, 14, 13, 21);
  icon.line(19, 14, 19, 21);
  icon.rect(10, 21, 12, 4);
  icon.line(7, 28, 25, 28);
  return icon;
}

constexpr Bitmap sudoku() {
  Bitmap icon;
  icon.rect(4, 4, 24, 24);
  icon.line(12, 4, 12, 27);
  icon.line(20, 4, 20, 27);
  icon.line(4, 12, 27, 12);
  icon.line(4, 20, 27, 20);
  icon.filledRect(7, 7, 2, 2);
  icon.filledRect(15, 15, 2, 2);
  icon.filledRect(23, 23, 2, 2);
  return icon;
}

constexpr Bitmap gomoku() {
  Bitmap icon;
  for (int p = 6; p <= 26; p += 5) {
    icon.line(6, p, 26, p);
    icon.line(p, 6, p, 26);
  }
  icon.circle(11, 11, 3);
  icon.circle(21, 16, 3);
  icon.filledRect(14, 19, 5, 5);
  return icon;
}

constexpr Bitmap sokoban() {
  Bitmap icon;
  icon.rect(5, 5, 22, 22);
  icon.rect(9, 9, 14, 14);
  icon.line(9, 9, 22, 22);
  icon.line(22, 9, 9, 22);
  icon.circle(16, 16, 3);
  return icon;
}

constexpr Bitmap chineseChess() {
  Bitmap icon;
  icon.circle(11, 16, 8);
  icon.circle(22, 16, 8);
  icon.line(7, 12, 15, 20);
  icon.line(15, 12, 7, 20);
  icon.line(18, 12, 26, 20);
  icon.line(26, 12, 18, 20);
  return icon;
}

constexpr Bitmap minesweeper() {
  Bitmap icon;
  icon.circle(16, 16, 7);
  icon.filledRect(13, 13, 7, 7);
  icon.line(16, 3, 16, 8);
  icon.line(16, 24, 16, 29);
  icon.line(3, 16, 8, 16);
  icon.line(24, 16, 29, 16);
  icon.line(7, 7, 11, 11);
  icon.line(21, 21, 25, 25);
  icon.line(25, 7, 21, 11);
  icon.line(11, 21, 7, 25);
  return icon;
}

constexpr Bitmap game2048() {
  Bitmap icon;
  icon.rect(4, 4, 11, 11);
  icon.rect(17, 4, 11, 11);
  icon.rect(4, 17, 11, 11);
  icon.rect(17, 17, 11, 11);
  icon.line(7, 8, 11, 8);
  icon.line(11, 8, 7, 12);
  icon.line(20, 8, 24, 8);
  icon.line(20, 12, 24, 12);
  icon.line(8, 20, 8, 24);
  icon.line(20, 20, 24, 24);
  icon.line(24, 20, 20, 24);
  return icon;
}

constexpr Bitmap avatar() {
  Bitmap icon;
  icon.circle(16, 14, 10);
  icon.circle(12, 12, 1);
  icon.circle(20, 12, 1);
  icon.line(11, 18, 14, 20);
  icon.line(14, 20, 18, 20);
  icon.line(18, 20, 21, 18);
  icon.line(8, 25, 4, 29);
  icon.line(24, 25, 28, 29);
  return icon;
}

constexpr Bitmap airPage() {
  Bitmap icon;
  icon.rect(5, 3, 18, 26);
  icon.line(18, 3, 23, 8);
  icon.line(18, 3, 18, 8);
  icon.line(18, 8, 23, 8);
  icon.circle(25, 23, 2);
  icon.line(22, 19, 25, 16);
  icon.line(19, 17, 25, 11);
  return icon;
}

constexpr Bitmap buddy() {
  Bitmap icon;
  icon.line(8, 8, 5, 3);
  icon.line(24, 8, 27, 3);
  icon.circle(16, 16, 11);
  icon.filledRect(11, 13, 2, 2);
  icon.filledRect(19, 13, 2, 2);
  icon.line(13, 20, 16, 22);
  icon.line(16, 22, 19, 20);
  return icon;
}

constexpr Bitmap pixelSwitch() {
  Bitmap icon;
  icon.rect(4, 4, 24, 24);
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      if ((row + column) % 2 == 0) icon.filledRect(6 + column * 5, 6 + row * 5, 4, 4);
    }
  }
  return icon;
}

constexpr Bitmap standby() {
  Bitmap icon;
  for (int y = 3; y < 29; ++y) {
    for (int x = 3; x < 29; ++x) {
      const int outer = (x - 15) * (x - 15) + (y - 16) * (y - 16);
      const int cutout = (x - 20) * (x - 20) + (y - 12) * (y - 12);
      if (outer <= 12 * 12 && cutout > 10 * 10) icon.ink(x, y);
    }
  }
  return icon;
}

constexpr Bitmap weread() {
  Bitmap icon;
  icon.rect(4, 6, 24, 17);
  icon.line(10, 23, 7, 28);
  icon.line(10, 23, 14, 23);
  icon.circle(11, 14, 3);
  icon.circle(21, 14, 3);
  return icon;
}

inline constexpr Bitmap Transfer = transfer();
inline constexpr Bitmap Opds = opds();
inline constexpr Bitmap ReadingStats = readingStats();
inline constexpr Bitmap ReadingHeatmap = readingHeatmap();
inline constexpr Bitmap ReadingProfile = readingProfile();
inline constexpr Bitmap Achievements = achievements();
inline constexpr Bitmap Sudoku = sudoku();
inline constexpr Bitmap Gomoku = gomoku();
inline constexpr Bitmap Sokoban = sokoban();
inline constexpr Bitmap ChineseChess = chineseChess();
inline constexpr Bitmap Minesweeper = minesweeper();
inline constexpr Bitmap Game2048 = game2048();
inline constexpr Bitmap Avatar = avatar();
inline constexpr Bitmap AirPage = airPage();
inline constexpr Bitmap Buddy = buddy();
inline constexpr Bitmap PixelSwitch = pixelSwitch();
inline constexpr Bitmap Standby = standby();
inline constexpr Bitmap WeRead = weread();

constexpr const uint8_t* get(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Transfer:
      return Transfer.data.data();
    case UIIcon::Opds:
      return Opds.data.data();
    case UIIcon::ReadingStats:
      return ReadingStats.data.data();
    case UIIcon::ReadingHeatmap:
      return ReadingHeatmap.data.data();
    case UIIcon::ReadingProfile:
      return ReadingProfile.data.data();
    case UIIcon::Achievements:
      return Achievements.data.data();
    case UIIcon::Sudoku:
      return Sudoku.data.data();
    case UIIcon::Gomoku:
      return Gomoku.data.data();
    case UIIcon::Sokoban:
      return Sokoban.data.data();
#ifdef ENABLE_CHINESE_VERSION
    case UIIcon::ChineseChess:
      return ChineseChess.data.data();
    case UIIcon::WeRead:
      return WeRead.data.data();
#endif
    case UIIcon::Minesweeper:
      return Minesweeper.data.data();
    case UIIcon::Avatar:
      return Avatar.data.data();
    case UIIcon::Standby:
      return Standby.data.data();
    case UIIcon::Game2048:
      return Game2048.data.data();
    case UIIcon::Buddy:
      return Buddy.data.data();
    case UIIcon::PixelSwitch:
      return PixelSwitch.data.data();
    case UIIcon::AirPage:
      return AirPage.data.data();
    default:
      return nullptr;
  }
}

inline void draw(const GfxRenderer& renderer, const UIIcon icon, const int x, const int y, const int scale,
                 const bool inverted) {
  const uint8_t* bitmap = get(icon);
  if (!bitmap || scale <= 0) return;
  constexpr int rowBytes = size / 8;
  for (int row = 0; row < size; ++row) {
    for (int column = 0; column < size; ++column) {
      const uint8_t byte = bitmap[row * rowBytes + column / 8];
      if (((byte >> (7 - column % 8)) & 1U) != 0) continue;
      renderer.fillRect(x + column * scale, y + row * scale, scale, scale, !inverted);
    }
  }
}
}  // namespace InxAppIcons
