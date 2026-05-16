#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "../GameSaveDebouncer.h"
#include "MinesweeperBoard.h"

class MinesweeperGameActivity final : public Activity {
 public:
  MinesweeperGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          MinesweeperBoard::Difficulty difficulty, bool resume = false);
  ~MinesweeperGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Localized difficulty name; public so MinesweeperMenuActivity can reuse it.
  static const char* difficultyName(MinesweeperBoard::Difficulty d);

 private:
  enum class State : uint8_t { Playing, GameMenu, Won, Lost };

  State state = State::Playing;
  MinesweeperBoard board;
  MinesweeperBoard::Difficulty difficulty;

  uint8_t cursorR = 0;
  uint8_t cursorC = 0;
  bool flagMode = false;
  uint8_t hintsLeft = 3;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  bool resumeRequested = false;
  bool statsRecorded = false;

  uint8_t menuSel = 0;
  static constexpr uint8_t MENU_ITEM_COUNT = 7;

  // Layout (logical 480×800 portrait). Board occupies the same vertical slot
  // as Sudoku's grid for visual consistency; cell pixel size varies by board
  // dimensions so the total 432 px width is preserved.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_X = 24;
  static constexpr int BOARD_Y = 60;   // matches Sudoku GRID_Y
  static constexpr int BOARD_W = 432;  // matches Sudoku GRID_SIZE_PX
  // End-game screen anchors. The Playing screen only uses the board area and
  // the footer button hints — everything between board bottom (y=492) and the
  // footer is intentionally left blank.
  static constexpr int ENDGAME_HERO_Y = 524;   // "Cleared!" / "Boom!" headline top
  static constexpr int ENDGAME_STATS_Y = 588;  // top border of the 3-column stat row

  int cellSize() const { return BOARD_W / board.cols; }

  // Drawing
  void renderPlaying();
  void renderGameMenu();
  void renderEnd(bool won);
  void drawTitleBar();
  void drawBoard();
  void drawFooter();
  void drawCellContent(int cellX, int cellY, int size, const MinesweeperBoard::Cell& cell) const;
  void drawMine(int cellX, int cellY, int size, bool onDarkBg) const;
  void drawFlag(int cellX, int cellY, int size) const;
  void drawNumber(int cellX, int cellY, int size, uint8_t n) const;

  // Input
  void handleInputPlaying();
  void handleInputGameMenu();
  void handleInputEnd();
  void enterGameMenu();
  void runMenuItem(uint8_t i);
  void resumeFromMenu();

  // Game flow
  void moveCursor(int dr, int dc);
  void doDig();
  void doFlag();
  void doConfirmAction();  // dispatches dig/flag based on flagMode
  void onGameEnd(bool won);
  void useHint();
  void resetGameKeepLayout();  // "Restart" — clear reveals, keep mine layout
  void scheduleSave();
  void flushSave();
};
