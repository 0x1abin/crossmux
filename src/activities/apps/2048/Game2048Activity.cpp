#include "Game2048Activity.h"

#include <Arduino.h>
#include <I18n.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../util/ConfirmationActivity.h"
#include "../GameUi.h"
#include "Game2048Store.h"

namespace {

constexpr int kStatusFont = UI_12_FONT_ID;
constexpr int kBannerFont = NOTOSANS_18_FONT_ID;
constexpr int kBannerHintFont = UI_12_FONT_ID;

// Tile-value font tiers, picked by digit count so big numbers still fit.
int fontForExponent(uint8_t exponent) {
  const uint32_t value = 1u << exponent;
  if (value < 100) return NOTOSANS_18_FONT_ID;    // "2".."64" — biggest
  if (value < 1000) return NOTOSANS_16_FONT_ID;   // "128".."512"
  if (value < 10000) return NOTOSANS_14_FONT_ID;  // "1024".."8192"
  return NOTOSANS_12_FONT_ID;                     // "16384".."65536"
}

// Dithered grayscale ramp: empty / light / dark / black-with-inverted-text,
// with a thicker border for the 2048+ milestone tile. Color uses spatial
// dither (LightGray = 25%, DarkGray = 50%) on the BW framebuffer — no extra
// passes or flicker.
struct TileStyle {
  Color bg;         // Clear = no fill (empty cell)
  bool textBlack;   // false = white text drawn over a black/dither background
  int borderWidth;  // black outline width
};

TileStyle styleForExponent(uint8_t exponent) {
  if (exponent == 0) return {Color::Clear, true, 1};
  if (exponent <= 2) return {Color::LightGray, true, 1};   // 2, 4
  if (exponent <= 5) return {Color::DarkGray, true, 1};    // 8, 16, 32
  if (exponent <= 10) return {Color::Black, false, 1};     // 64 .. 1024
  return {Color::Black, false, 3};                          // 2048+
}

}  // namespace

void Game2048Activity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  Game2048SaveSlot slot;
  if (Game2048Store::hasInProgress() && Game2048Store::load(slot)) {
    for (int r = 0; r < Game2048Board::SIZE; ++r) {
      for (int c = 0; c < Game2048Board::SIZE; ++c) {
        board_.set(r, c, slot.cells[r][c]);
      }
    }
    score_ = slot.score;
    won_ = slot.won;
    gameOver_ = board_.isStuck();
  } else {
    newGame();
  }
  requestUpdate();
}

void Game2048Activity::onExit() {
  saveDebouncer_.clear();
  persistNow();
  Activity::onExit();
}

void Game2048Activity::loop() {
  if (saveDebouncer_.consumeIfDue(millis())) {
    persistNow();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    persistNow();
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (gameOver_) {
      // Board stuck — banner already says "Press New to start again", no need to ask again.
      newGame();
      persistNow();
      requestUpdate();
    } else {
      promptNewGameConfirm();
    }
    return;
  }

  if (gameOver_) return;  // No swipes when stuck — Confirm restarts.

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    handleSlide(Game2048Board::Direction::Up);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    handleSlide(Game2048Board::Direction::Down);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    handleSlide(Game2048Board::Direction::Left);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    handleSlide(Game2048Board::Direction::Right);
  }
}

void Game2048Activity::newGame() {
  board_.reset();
  score_ = 0;
  won_ = false;
  gameOver_ = false;
}

void Game2048Activity::handleSlide(Game2048Board::Direction d) {
  uint32_t delta = 0;
  if (!board_.slide(d, delta)) return;
  score_ += delta;
  if (!won_ && board_.isWon()) won_ = true;
  gameOver_ = board_.isStuck();
  saveDebouncer_.schedule(millis());
  requestUpdate();
}

void Game2048Activity::promptNewGameConfirm() {
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_2048_CONFIRM_NEW),
                                                                tr(STR_2048_CONFIRM_BODY)),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             requestUpdate();
                             return;
                           }
                           newGame();
                           persistNow();
                           requestUpdate();
                         });
}

void Game2048Activity::persistNow() {
  Game2048SaveSlot slot;
  for (int r = 0; r < Game2048Board::SIZE; ++r) {
    for (int c = 0; c < Game2048Board::SIZE; ++c) {
      slot.cells[r][c] = board_.at(r, c);
    }
  }
  slot.score = score_;
  slot.won = won_;
  Game2048Store::save(slot);
}

void Game2048Activity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  drawTitleBar();
  drawGrid();
  if (won_ || gameOver_) drawOverlayBanner();
  drawFooter();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void Game2048Activity::drawTitleBar() {
  const int sw = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H, sw - 1, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  // Left: title.
  renderer.drawText(kStatusFont, CONTENT_X, y, tr(STR_2048_TITLE));

  // Right: current score only — Best is intentionally hidden in-game to keep focus on play.
  char right[40];
  snprintf(right, sizeof(right), "%s %lu", tr(STR_2048_SCORE), static_cast<unsigned long>(score_));
  const int rw = renderer.getTextWidth(kStatusFont, right);
  renderer.drawText(kStatusFont, sw - CONTENT_X - rw, y, right);
}

void Game2048Activity::drawGrid() {
  const int sw = renderer.getScreenWidth();
  const int ox = (sw - GRID_W) / 2;
  const int oy = GRID_TOP;
  for (int r = 0; r < Game2048Board::SIZE; ++r) {
    for (int c = 0; c < Game2048Board::SIZE; ++c) {
      const int x = ox + c * (CELL_PX + GAP_PX);
      const int y = oy + r * (CELL_PX + GAP_PX);
      drawTile(x, y, board_.at(r, c));
    }
  }
}

void Game2048Activity::drawTile(int x, int y, uint8_t exponent) {
  const TileStyle style = styleForExponent(exponent);

  if (style.bg != Color::Clear) {
    renderer.fillRectDither(x, y, CELL_PX, CELL_PX, style.bg);
  }
  renderer.drawRect(x, y, CELL_PX, CELL_PX, style.borderWidth, true);

  if (exponent != 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(1u) << exponent);
    const int fontId = fontForExponent(exponent);
    const int tw = renderer.getTextWidth(fontId, buf);
    const int th = renderer.getTextHeight(fontId);
    const int tx = x + (CELL_PX - tw) / 2;
    const int ty = y + (CELL_PX - th) / 2;
    renderer.drawText(fontId, tx, ty, buf, style.textBlack);
  }
}

void Game2048Activity::drawOverlayBanner() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  const char* title = gameOver_ ? tr(STR_2048_GAME_OVER) : tr(STR_2048_YOU_WIN);
  const char* hint = tr(STR_2048_HINT_RESTART);

  const int titleH = renderer.getTextHeight(kBannerFont);
  const int hintH = renderer.getTextHeight(kBannerHintFont);
  const int titleW = renderer.getTextWidth(kBannerFont, title);
  const int hintW = renderer.getTextWidth(kBannerHintFont, hint);
  const int innerW = (titleW > hintW ? titleW : hintW) + 48;
  const int innerH = titleH + hintH + 36;
  const int boxX = (sw - innerW) / 2;
  const int boxY = (sh - innerH) / 2;

  // White fill to mask the grid behind, then a thick border.
  renderer.fillRect(boxX, boxY, innerW, innerH, false);
  renderer.drawRect(boxX, boxY, innerW, innerH, 3, true);

  renderer.drawText(kBannerFont, (sw - titleW) / 2, boxY + 12, title);
  renderer.drawText(kBannerHintFont, (sw - hintW) / 2, boxY + 12 + titleH + 8, hint);
}

void Game2048Activity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_2048_NEW), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
