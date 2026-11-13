#pragma once

#include <FreeInkUI.h>

#include <algorithm>

#include "UiHighDpiProfile.h"

namespace BatteryIcon {
// Keep the legacy pixels, including the terminal, at an integer scale.
inline void draw(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, uint16_t percentage, const bool charging,
                 const bool segmented) {
  constexpr int scale = UiHighDpiProfile::batteryScale;
  constexpr int height = 12;
  const int width = rect.width / scale - 1;
  const auto fill = [&](const int x, const int y, const int w, const int h, const bool black = true) {
    target.fill({static_cast<int16_t>(rect.x + x * scale), static_cast<int16_t>(rect.y + y * scale),
                 static_cast<int16_t>(w * scale), static_cast<int16_t>(h * scale)},
                freeink::ui::Paint::solid(black ? freeink::ui::Color::Black : freeink::ui::Color::White));
  };

  fill(1, 0, width - 3, 1);
  fill(1, height - 1, width - 3, 1);
  fill(0, 1, 1, height - 2);
  fill(width - 2, 1, 1, height - 2);
  fill(width - 1, 3, 1, 1);
  fill(width - 1, height - 4, 1, 1);
  fill(width, 4, 1, height - 8);

  percentage = std::min<uint16_t>(percentage, 100);
  if (segmented && !charging) {
    if (percentage > 10) fill(2, 2, 3, height - 4);
    if (percentage > 40) fill(6, 2, 3, height - 4);
    if (percentage > 70) fill(10, 2, 3, height - 4);
  } else {
    const int maxFillWidth = width - 5;
    int filledWidth = std::min(maxFillWidth, percentage * maxFillWidth / 100 + 1);
    if (charging) filledWidth = segmented ? maxFillWidth : std::min(maxFillWidth, std::max(8, filledWidth));
    fill(2, 2, filledWidth, height - 4);
  }

  if (charging) {
    static constexpr uint8_t bolt[][2] = {{4, 5}, {3, 4}, {2, 5}, {3, 4}, {2, 3}, {1, 4}, {2, 3}, {1, 2}};
    for (int row = 0; row < 8; ++row) {
      fill(4 + bolt[row][0], 2 + row, bolt[row][1] - bolt[row][0] + 1, 1, false);
    }
  }
}
}  // namespace BatteryIcon
