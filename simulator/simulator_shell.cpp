#include "simulator_shell.h"

#include <HalDisplay.h>

#include <cmath>

namespace simulator {

namespace {

// All constants are in unscaled pixels at 1× and derived from the published
// Xteink X4 spec: 480×800 @ 4.3" / 220 PPI / 114 × 69 × 5.9 mm. At 220 PPI
// that's 8.66 px/mm, so the device pixel envelope is 987 × 598 px, leaving
// 59 px horizontal bezels and a 47/140 top/bottom split (≈1:3, to fit the
// front-face button row near the bottom).
constexpr int BEZEL_LEFT = 59;
constexpr int BEZEL_RIGHT = 59;
constexpr int BEZEL_TOP = 47;
constexpr int BEZEL_BOTTOM = 140;
// ~5 mm chamfer — matches the X4 product photo.
constexpr int BEZEL_CORNER_RADIUS = 44;

// Right-side buttons. Three physical buttons per the spec sheet:
//   Power           — short, near the top  (~18% device height)
//   Previous Page   — longer, middle       (~30%)
//   Next Page       — longer, just below   (~42%)
// All y values are well inside [CORNER_RADIUS, windowH-CORNER_RADIUS] so the
// rounded corners don't clip the bumps.
constexpr int RIGHT_BUTTON_W = 8;
constexpr int POWER_Y = 158;
constexpr int POWER_H = 40;
constexpr int RIGHT_PREV_PAGE_Y = 278;
constexpr int RIGHT_PREV_PAGE_H = 55;
constexpr int RIGHT_NEXT_PAGE_Y = 386;
constexpr int RIGHT_NEXT_PAGE_H = 55;

// Front-face bottom buttons. Four thin slots arranged 2+2: Back + OK are flush
// (group-internal gap = 2 px), then a wider gap, then Next Page + Previous Page
// also flush. From a step back the row reads as two long bars. The y offset
// places them deep in the bottom bezel — ~4 mm above the device's bottom edge,
// matching the product photo rather than floating mid-bezel.
constexpr int FRONT_BUTTON_W = 80;
constexpr int FRONT_BUTTON_H = 6;
constexpr int FRONT_BUTTON_INNER_GAP = 2;
constexpr int FRONT_BUTTON_GROUP_GAP = 60;
constexpr int FRONT_BUTTON_Y_OFFSET = 100;  // px below eink bottom edge

// Fills a rounded rectangle with the renderer's current draw color. The four
// corners are antialiased by mapping each edge-band pixel's circle coverage
// to an alpha value. This function manages its own blend-mode state so it can
// be called regardless of what the renderer was in.
//
// Coverage rule for a corner pixel whose center is at distance d from the
// corner circle's center:
//   d ≤ rad − 0.5  → fully inside, alpha=255
//   d ≥ rad + 0.5  → outside, skip
//   otherwise      → alpha = round((rad + 0.5 − d) × 255)   (1 px-wide band)
void fillRoundedRect(SDL_Renderer* r, int x, int y, int w, int h, int rad) {
  if (!r || w <= 0 || h <= 0) return;
  if (rad < 0) rad = 0;
  if (rad * 2 > w) rad = w / 2;
  if (rad * 2 > h) rad = h / 2;

  // Body: three rectangles cover everything except the four corner squares.
  SDL_Rect mid{x, y + rad, w, h - 2 * rad};
  SDL_Rect top{x + rad, y, w - 2 * rad, rad};
  SDL_Rect bot{x + rad, y + h - rad, w - 2 * rad, rad};
  SDL_RenderFillRect(r, &mid);
  SDL_RenderFillRect(r, &top);
  SDL_RenderFillRect(r, &bot);

  Uint8 cr, cg, cb, ca;
  SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
  SDL_BlendMode prevBlend{};
  SDL_GetRenderDrawBlendMode(r, &prevBlend);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

  for (int j = 0; j < rad; ++j) {
    for (int i = 0; i < rad; ++i) {
      const double dx = (i + 0.5) - rad;
      const double dy = (j + 0.5) - rad;
      const double d = std::sqrt(dx * dx + dy * dy);
      Uint8 a;
      if (d <= rad - 0.5) {
        a = 255;
      } else if (d >= rad + 0.5) {
        continue;
      } else {
        a = static_cast<Uint8>(std::round((rad + 0.5 - d) * 255.0));
      }
      SDL_SetRenderDrawColor(r, cr, cg, cb, a);
      SDL_Rect corners[4] = {
          {x + i,           y + j,           1, 1},  // TL
          {x + w - 1 - i,   y + j,           1, 1},  // TR
          {x + i,           y + h - 1 - j,   1, 1},  // BL
          {x + w - 1 - i,   y + h - 1 - j,   1, 1},  // BR
      };
      for (const auto& c : corners) SDL_RenderFillRect(r, &c);
    }
  }

  SDL_SetRenderDrawBlendMode(r, prevBlend);
  SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
}

}  // namespace

ShellLayout computeShellLayout(bool showShell, int scale) {
  if (scale < 1) scale = 1;
  ShellLayout l{};
  l.scale = scale;
  l.showShell = showShell;
  l.einkW = HalDisplay::DISPLAY_HEIGHT * scale;  // rotated: portrait width = source height
  l.einkH = HalDisplay::DISPLAY_WIDTH * scale;
  if (showShell) {
    l.einkX = BEZEL_LEFT * scale;
    l.einkY = BEZEL_TOP * scale;
    l.windowW = l.einkX + l.einkW + BEZEL_RIGHT * scale;
    l.windowH = l.einkY + l.einkH + BEZEL_BOTTOM * scale;
  } else {
    l.einkX = 0;
    l.einkY = 0;
    l.windowW = l.einkW;
    l.windowH = l.einkH;
  }
  return l;
}

void drawShell(SDL_Renderer* r, const ShellLayout& layout) {
  if (!r || !layout.showShell) return;
  const int s = layout.scale;

  // Body: deep-black rounded rectangle sitting on the renderer's clear color.
  // The corners that fall outside this shape show the SDL_RenderClear backdrop
  // (a neutral mid-grey set in SimulatorWindow::present), giving the device a
  // floating-on-canvas look reminiscent of Xcode's iPhone simulator.
  SDL_SetRenderDrawColor(r, 0x18, 0x18, 0x18, 0xFF);
  fillRoundedRect(r, 0, 0, layout.windowW, layout.windowH, BEZEL_CORNER_RADIUS * s);

  // Right-side button bumps — three buttons, drawn flush with the right edge.
  // y ranges are inside the safe band (above bottom rounded corners, below top).
  SDL_SetRenderDrawColor(r, 0x05, 0x05, 0x05, 0xFF);
  SDL_Rect power{layout.windowW - RIGHT_BUTTON_W * s,
                 POWER_Y * s,
                 RIGHT_BUTTON_W * s,
                 POWER_H * s};
  SDL_RenderFillRect(r, &power);
  SDL_Rect rPrev{layout.windowW - RIGHT_BUTTON_W * s,
                 RIGHT_PREV_PAGE_Y * s,
                 RIGHT_BUTTON_W * s,
                 RIGHT_PREV_PAGE_H * s};
  SDL_RenderFillRect(r, &rPrev);
  SDL_Rect rNext{layout.windowW - RIGHT_BUTTON_W * s,
                 RIGHT_NEXT_PAGE_Y * s,
                 RIGHT_BUTTON_W * s,
                 RIGHT_NEXT_PAGE_H * s};
  SDL_RenderFillRect(r, &rNext);

  // Front-face bottom buttons — 2+2 layout: Back + OK (flush) | gap |
  // Next + Prev (flush). The advance between consecutive buttons is the
  // inner gap, except between buttons 1 and 2 where it's the group gap.
  SDL_SetRenderDrawColor(r, 0x40, 0x40, 0x40, 0xFF);
  const int btnW = FRONT_BUTTON_W * s;
  const int btnH = FRONT_BUTTON_H * s;
  const int innerGap = FRONT_BUTTON_INNER_GAP * s;
  const int groupGap = FRONT_BUTTON_GROUP_GAP * s;
  const int totalW = 4 * btnW + 2 * innerGap + groupGap;
  const int rowY = layout.einkY + layout.einkH + FRONT_BUTTON_Y_OFFSET * s;
  int x = layout.einkX + (layout.einkW - totalW) / 2;
  for (int i = 0; i < 4; ++i) {
    SDL_Rect btn{x, rowY, btnW, btnH};
    SDL_RenderFillRect(r, &btn);
    x += btnW + (i == 1 ? groupGap : innerGap);
  }
}

}  // namespace simulator
