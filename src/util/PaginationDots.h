#pragma once

#include <GfxRenderer.h>

// Bottom page-indicator dots, matching the Standby face indicator style: one dot
// per page, filled for the current page, 1px-outlined for the others, centered
// horizontally. `topY` is the top edge of the dot row so each caller controls its
// own vertical placement (Standby sits them near the bottom edge; the Apps menu
// drops them into the gap above the button hints).
inline void drawPaginationDots(const GfxRenderer& renderer, int centerWidth, int topY, int total, int current) {
  if (total <= 0) return;
  constexpr int kDotDiameter = 6;
  constexpr int kDotStep = 20;  // distance between dot centers (6px dot + 14px gap)
  const int totalWidth = total * kDotDiameter + (total - 1) * (kDotStep - kDotDiameter);
  const int startX = (centerWidth - totalWidth) / 2;
  for (int i = 0; i < total; ++i) {
    const int x = startX + i * kDotStep;
    if (i == current) {
      renderer.fillRoundedRect(x, topY, kDotDiameter, kDotDiameter, kDotDiameter / 2, Color::Black);
    } else {
      renderer.drawRoundedRect(x, topY, kDotDiameter, kDotDiameter, /*lineWidth=*/1, kDotDiameter / 2,
                               /*state=*/true);
    }
  }
}
