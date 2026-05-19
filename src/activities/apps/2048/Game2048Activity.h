#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "../GameSaveDebouncer.h"
#include "Game2048Board.h"

// Classic 2048: swipe to slide and merge tiles. Single-Activity app (no menu) — entering
// resumes any in-progress game; Confirm starts a fresh game in place. Auto-saves with a
// 1.5s debounce so SPIFFS isn't hit on every move.
class Game2048Activity final : public Activity {
 public:
  Game2048Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Game2048", renderer, mappedInput) {}
  ~Game2048Activity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Portrait layout (480 wide). Grid: 4 cells × 96 + 3 gaps × 8 = 408, centered (left inset 36).
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int CELL_PX = 96;
  static constexpr int GAP_PX = 8;
  static constexpr int GRID_W = 4 * CELL_PX + 3 * GAP_PX;  // 408
  // Pad above the grid so the 4×4 board sits visually centered between the title bar (36)
  // and the footer (~80). 800 - 80 footer - 36 title - 408 grid = 276 free → ~138 top
  // padding centers exactly; 84 here biases the board slightly upward for a balanced look.
  static constexpr int GRID_TOP = TITLE_BAR_H + 84;

  Game2048Board board_;
  uint32_t score_ = 0;
  bool won_ = false;       // sticky: any tile ever reached 2048 (persisted)
  bool gameOver_ = false;  // derived from board_.isStuck() — not persisted
  GameSaveDebouncer saveDebouncer_;

  void newGame();
  void persistNow();
  void handleSlide(Game2048Board::Direction d);
  void promptNewGameConfirm();

  void drawTitleBar();
  void drawGrid();
  void drawTile(int x, int y, uint8_t exponent);
  void drawOverlayBanner();
  void drawFooter();
};
