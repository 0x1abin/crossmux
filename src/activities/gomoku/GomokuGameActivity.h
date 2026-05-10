#pragma once

#include <cstdint>

#include "../Activity.h"
#include "GomokuBoard.h"
#include "GomokuStore.h"

class GomokuGameActivity final : public Activity {
 public:
  GomokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GomokuMode mode, uint8_t boardSize,
                     bool resume);
  ~GomokuGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, GameMenu, GameOver };

  // Layout anchors — y values for top-of-section. Title bar / board area /
  // mode line align with SudokuGameActivity; info-panel content sits a bit
  // lower than Sudoku's palette for visual balance with the mode line.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_AREA_Y = 60;        // matches Sudoku GRID_Y
  static constexpr int INFO_PANEL_Y = 540;       // 30 px below Sudoku PALETTE_Y
  static constexpr int MODE_LINE_Y = 702;        // matches Sudoku MODE_LINE_Y
  static constexpr uint8_t MENU_ITEM_COUNT = 5;  // Resume / Undo / Resign / New Game / Exit

  State state = State::Playing;
  GomokuMode mode = GomokuMode::TwoPlayer;

  GomokuBoard board;
  uint8_t cursorR = 7;
  uint8_t cursorC = 7;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  uint32_t pendingSaveAtMs = 0;
  bool resumeRequested = false;

  uint8_t menuSel = 0;

  // End-game outcome (set when state transitions to GameOver).
  // Keeps recording idempotent: stats are written exactly once per game.
  bool statsRecorded = false;
  bool resignedFlag = false;  // resign overrides natural detection
  GomokuBoard::Stone resignWinner = GomokuBoard::Stone::Empty;

  // Geometry helpers (depend on boardSize).
  int boardPitch() const;
  int boardOriginX() const;
  int boardOriginY() const;
  int stoneRadius() const;
  void intersectionXY(uint8_t r, uint8_t c, int* x, int* y) const;
  // Format intersection as "K10" (column letter A-O + 1-indexed row from bottom).
  void coordToText(uint8_t r, uint8_t c, char* out, size_t outLen) const;

  // Drawing
  void renderPlaying();
  void renderGameMenu();
  void renderGameOver();
  void drawTitleBar();
  void drawBoard();
  void drawInfoPanel();
  void drawModeLine();
  void drawFooter();
  void drawStone(int cx, int cy, int radius, bool isBlack) const;
  void drawWinLine();

  // Input
  void handleInputPlaying();
  void handleInputGameMenu();
  void handleInputGameOver();
  void enterGameMenu();
  void runMenuItem(uint8_t i);
  void resumeFromMenu();

  // Game flow
  void moveCursor(int dr, int dc);
  void doPlace();
  void onGameOver();  // Detects natural Win/Draw and triggers state change.
  void scheduleSave();
  void flushSave();
  void formatTime(uint32_t ms, char* out, size_t outLen) const;
};
