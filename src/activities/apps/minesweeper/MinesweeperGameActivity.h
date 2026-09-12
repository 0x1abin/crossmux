#pragma once

#include <cstdint>

#include "MinesweeperBoard.h"
#include "activities/Activity.h"
#include "activities/apps/GameSaveDebouncer.h"
#include "components/OptionPopup.h"

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

  OptionPopup gameMenu;
  static constexpr uint8_t MENU_ITEM_COUNT = 7;

  // Portrait layout. Board occupies the same vertical slot as Sudoku's grid
  // for visual consistency; cell pixel size varies by board dimensions so the
  // total 432 px width is preserved.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_Y = 60;   // matches Sudoku GRID_Y
  static constexpr int BOARD_W = 432;  // matches Sudoku GRID_SIZE_PX
  // Bottom action bar with separate Dig and Flag buttons. Touch on the board
  // only aims the cursor, so digging and flagging each need their own visible
  // target; a single mode-toggle button made the current mode invisible at the
  // moment of use. Sized from the panel height (this firmware ships several
  // targets) and measured UP FROM THE SCREEN BOTTOM.
  static constexpr int ACTION_BAR_H_FRAC = 10;      // bar height = screenH / 10
  static constexpr int ACTION_BAR_MIN_H = 40;       // stay a fingertip target
  static constexpr int ACTION_BAR_BOTTOM_FRAC = 4;  // bottom gap = screenH / 4 / 10
  static constexpr int ACTION_BAR_GAP = 8;          // >= the 6 px control spacing rule
  // End-game screen anchors. The Playing screen uses the board area plus the
  // action bar; everything between them stays blank.
  static constexpr int ENDGAME_HERO_Y = 524;   // "Cleared!" / "Boom!" headline top
  static constexpr int ENDGAME_STATS_Y = 588;  // top border of the 3-column stat row

  int cellSize() const { return BOARD_W / board.cols; }

  // Drawing
  void renderPlaying();
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

  // Bottom action bar geometry, measured up from the screen bottom and scaled
  // to the panel so it stays on-screen on every target.
  int actionBarH() const;
  int actionBarY() const;
  Rect digButtonRect() const;
  Rect flagButtonRect() const;
  void resetGameKeepLayout();  // "Restart" — clear reveals, keep mine layout
  void scheduleSave();
  void flushSave();
};
