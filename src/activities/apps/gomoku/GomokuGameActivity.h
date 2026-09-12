#pragma once

#include <cstdint>

#include "GomokuBoard.h"
#include "GomokuStore.h"
#include "activities/Activity.h"
#include "activities/apps/GameSaveDebouncer.h"
#include "components/OptionPopup.h"

class GomokuGameActivity final : public Activity {
 public:
  GomokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GomokuMode mode, uint8_t boardSize,
                     bool resume, GomokuAiLevel aiLevel = GomokuAiLevel::Medium);
  ~GomokuGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, GameMenu, GameOver };

  // Layout anchors — y values for top-of-section. Title bar / board area /
  // mode line align with SudokuGameActivity.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_AREA_Y = 60;  // matches Sudoku GRID_Y
  // Bottom action bar, measured UP FROM THE SCREEN BOTTOM as a fraction of the
  // panel height. This firmware ships many targets with different panel sizes,
  // so a bar pinned to absolute pixel offsets would land off-screen or overlap
  // the board somewhere. Everything here scales with getScreenHeight().
  static constexpr int ACTION_BAR_H_FRAC = 10;      // bar height = screenH / 10
  static constexpr int ACTION_BAR_MIN_H = 40;       // never smaller than a fingertip
  static constexpr int ACTION_BAR_BOTTOM_FRAC = 4;  // bottom gap = screenH / 4 / 10
  static constexpr uint8_t MENU_ITEM_COUNT = 5;     // Resume / Undo / Resign / New Game / Exit

  State state = State::Playing;
  GomokuMode mode = GomokuMode::TwoPlayer;
  GomokuAiLevel aiLevel = GomokuAiLevel::Medium;
  // AI plays White (player is Black, first to move). Fixed for now.
  static constexpr GomokuBoard::Stone kAiSide = GomokuBoard::Stone::White;

  GomokuBoard board;
  uint8_t cursorR = 7;
  uint8_t cursorC = 7;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  bool resumeRequested = false;
  // Two-stage AI move so the "Thinking..." frame reaches the e-ink before
  // search begins: doPlace() arms it, loop() shows it, the next loop runs AI.
  bool aiThinkingArmed = false;
  bool aiThinkingShown = false;

  OptionPopup gameMenu;

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
  // Screen-space touch zones. Touch aims the cursor only; the bottom action bar
  // commits the stone. One geometry source for both drawing and hit-testing.
  Rect boardTouchRect() const;
  // Bottom action bar, measured up from the screen bottom (see the constants).
  int actionBarH() const;
  int actionBarY() const;
  Rect actionBarRect() const;

  // Drawing
  void renderPlaying();
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
  // True when it's the AI's turn (mode==VsAi, AI's color, game not over).
  bool aiToMove() const;
  // Run AI search and place its move. Caller must verify aiToMove().
  void runAiTurn();
};
