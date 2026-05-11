#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "../GameSaveDebouncer.h"
#include "SudokuBoard.h"

class SudokuGameActivity final : public Activity {
 public:
  SudokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, SudokuBoard::Difficulty difficulty,
                     bool resume = false);
  ~SudokuGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Localized difficulty name; public so SudokuMenuActivity can reuse it.
  static const char* difficultyName(SudokuBoard::Difficulty d);

 private:
  enum class State : uint8_t { Generating, Playing, GameMenu, Won };
  enum class Focus : uint8_t { Grid, Palette };

  State state = State::Generating;
  Focus focus = Focus::Grid;

  SudokuBoard board;
  SudokuBoard::Difficulty difficulty;

  uint8_t cursorR = 4;
  uint8_t cursorC = 4;
  uint8_t paletteIdx = 0;       // 0..8 = digits 1..9, 9 = erase
  uint8_t lastPlacedDigit = 1;  // pre-select on entering Palette focus
  bool notesMode = false;
  uint8_t mistakes = 0;
  uint8_t hintsLeft = 3;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  bool resumeRequested = false;
  bool generationDone = false;  // Generator runs once on first loop tick

  // Drawing
  void renderGenerating();
  void renderPlaying();
  void renderGameMenu();
  void renderWon();
  void drawTitleBar();
  void drawGrid(int x0, int y0);
  void drawPalette(int x0, int y0);
  void drawModeLine();
  void drawFooter();
  void drawNotes(int cellX, int cellY, int cellSize, uint16_t notesBitmap);

  // Input
  void handleInputPlaying();
  void handleInputGameMenu();
  void handleInputWon();
  void enterGameMenu();
  void runMenuItem(uint8_t i);
  void onSolved();

  // Helpers
  void moveCursor(int dr, int dc);
  void movePalette(int dr, int dc);
  void enterPaletteFocus();
  void exitPaletteFocus(bool place);  // place=true → commit; false → cancel
  void doPlace(uint8_t digit);        // digit 0 = erase
  bool cellHasError(uint8_t r, uint8_t c) const;
  void scheduleSave();
  void flushSave();
  void useHint();
  void resetGame();
  void resumeFromMenu();  // state = Playing + reset timer base
  uint8_t menuSel = 0;
  static constexpr uint8_t MENU_ITEM_COUNT = 7;

  // Layout (logical 480×800 portrait). Single source of truth for the screen.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int CELL_PX = 48;
  static constexpr int GRID_SIZE_PX = CELL_PX * 9;  // 432
  static constexpr int GRID_X = CONTENT_X;
  static constexpr int GRID_Y = TITLE_BAR_H + CONTENT_X;  // 60: gap below title bar matches left margin
  static constexpr int PALETTE_CELL_W = 80;
  static constexpr int PALETTE_CELL_H = PALETTE_CELL_W;  // square palette cells
  static constexpr int PALETTE_GAP = 8;
  static constexpr int PALETTE_W = PALETTE_CELL_W * 5 + PALETTE_GAP * 4;  // 432
  static constexpr int PALETTE_H = PALETTE_CELL_H * 2 + PALETTE_GAP;      // 168
  static constexpr int PALETTE_X = CONTENT_X;
  static constexpr int PALETTE_Y = GRID_Y + GRID_SIZE_PX + CONTENT_X;  // 516: gap below grid matches left margin
  static constexpr int MODE_LINE_Y = PALETTE_Y + PALETTE_H + 18;       // 702
};
