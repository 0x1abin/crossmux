#include "simulator_overlay.h"

#include "simulator_bitmap_font.h"

namespace simulator {

namespace {

constexpr int PANEL_W = 360;
constexpr int PANEL_H = 200;
constexpr int PADDING = 18;
constexpr int FONT_SCALE = 2;       // 5x7 font → 10×14 px per glyph
constexpr int FONT_SCALE_HINT = 1;  // hint line at the bottom of the panel

void fillRect(SDL_Renderer* r, int x, int y, int w, int h,
              uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
  SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
  SDL_Rect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

void drawCheckbox(SDL_Renderer* r, int x, int y, int size, bool checked) {
  // Outer border.
  fillRect(r, x, y, size, size, 0xE0, 0xE0, 0xE0, 0xFF);
  fillRect(r, x + 2, y + 2, size - 4, size - 4, 0x10, 0x10, 0x10, 0xFF);
  if (checked) {
    // Simple plus/check: two crossing bars.
    const int pad = 4;
    fillRect(r, x + pad, y + size / 2 - 1, size - pad * 2, 3, 0xE0, 0xE0, 0xE0, 0xFF);
    fillRect(r, x + size / 2 - 1, y + pad, 3, size - pad * 2, 0xE0, 0xE0, 0xE0, 0xFF);
  }
}

}  // namespace

void SettingsOverlay::draw(SDL_Renderer* r, int winW, int winH) {
  if (!visible_ || !r) return;

  SDL_BlendMode previousBlend{};
  SDL_GetRenderDrawBlendMode(r, &previousBlend);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

  // Dimmer over the whole window.
  fillRect(r, 0, 0, winW, winH, 0x00, 0x00, 0x00, 0x80);

  // Centred panel.
  const int panelX = (winW - PANEL_W) / 2;
  const int panelY = (winH - PANEL_H) / 2;
  fillRect(r, panelX, panelY, PANEL_W, PANEL_H, 0x1A, 0x1A, 0x1A, 0xF0);
  // Thin light border for definition.
  fillRect(r, panelX, panelY, PANEL_W, 2, 0xC8, 0xC8, 0xC8, 0xFF);
  fillRect(r, panelX, panelY + PANEL_H - 2, PANEL_W, 2, 0xC8, 0xC8, 0xC8, 0xFF);
  fillRect(r, panelX, panelY, 2, PANEL_H, 0xC8, 0xC8, 0xC8, 0xFF);
  fillRect(r, panelX + PANEL_W - 2, panelY, 2, PANEL_H, 0xC8, 0xC8, 0xC8, 0xFF);

  // Title.
  const char* title = "Simulator Settings";
  const int titleW = measureText5x7(title, FONT_SCALE);
  drawText5x7(r, panelX + (PANEL_W - titleW) / 2, panelY + PADDING, FONT_SCALE,
              0xF0, 0xF0, 0xF0, title);

  // Divider.
  fillRect(r, panelX + PADDING, panelY + PADDING + 7 * FONT_SCALE + 10,
           PANEL_W - PADDING * 2, 1, 0x55, 0x55, 0x55, 0xFF);

  // "Show device shell" row.
  const int rowY = panelY + PADDING + 7 * FONT_SCALE + 24;
  const int checkboxSize = 7 * FONT_SCALE;  // matches glyph height
  const int checkboxX = panelX + PADDING;
  const int labelX = checkboxX + checkboxSize + 12;
  drawCheckbox(r, checkboxX, rowY, checkboxSize, showShell_);
  drawText5x7(r, labelX, rowY, FONT_SCALE, 0xF0, 0xF0, 0xF0, "Show device shell");

  // Cache hit-test rect: covers the full row width so the user can click
  // anywhere on the line (not just the tiny checkbox square).
  shellRowRect_ = SDL_Rect{checkboxX,
                           rowY,
                           PANEL_W - PADDING * 2,
                           checkboxSize};

  // Hint at the bottom of the panel.
  const char* hint = "F1 to close   click row to toggle";
  const int hintW = measureText5x7(hint, FONT_SCALE_HINT);
  drawText5x7(r, panelX + (PANEL_W - hintW) / 2,
              panelY + PANEL_H - PADDING - 7 * FONT_SCALE_HINT,
              FONT_SCALE_HINT, 0x90, 0x90, 0x90, hint);

  SDL_SetRenderDrawBlendMode(r, previousBlend);
}

bool SettingsOverlay::handleClick(int x, int y) {
  if (!visible_) return false;
  const SDL_Rect& r = shellRowRect_;
  if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h) return false;
  showShell_ = !showShell_;
  return true;
}

}  // namespace simulator
