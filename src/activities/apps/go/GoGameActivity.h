#pragma once

#include <cstdint>

#include "GoStore.h"
#include "activities/Activity.h"
#include "activities/apps/GameSaveDebouncer.h"
#include "components/OptionPopup.h"
#include "engine/ai.h"

class GoGameActivity final : public Activity {
 public:
  GoGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GoMode mode, GoDifficulty difficulty,
                 bool resume);
  ~GoGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, GameMenu, GameOver };

  // Layout anchors — mirror GomokuGameActivity (vertical portrait panel).
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_AREA_Y = 60;
  static constexpr int ACTION_BAR_H_FRAC = 10;      // bar height = screenH / 10
  static constexpr int ACTION_BAR_MIN_H = 40;       // never smaller than a fingertip
  static constexpr int ACTION_BAR_BOTTOM_FRAC = 4;  // bottom gap = screenH / 4 / 10
  static constexpr uint8_t MENU_ITEM_COUNT = 6;     // Resume / Undo / Pass / Resign / New Game / Exit
  static constexpr uint8_t kPassMove = 81;          // pass encoding in moveLog
  static constexpr uint8_t kMaxMoveLog = 100;

  static constexpr uint8_t kBoardSize = 9;

  State state = State::Playing;
  GoMode mode = GoMode::TwoPlayer;
  GoDifficulty difficulty = GoDifficulty::Kyu12;
  Game game;
  AI ai;
  uint8_t cursorX = 4;
  uint8_t cursorY = 4;
  uint32_t elapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  bool resumeRequested = false;
  bool statsRecorded = false;
  bool aiThinkingArmed = false;
  bool aiThinkingShown = false;
  // AI search telemetry for the info panel (last think()).
  uint8_t lastAiPct = 0;
  uint16_t lastAiVisits = 0;
  uint32_t lastAiMs = 0;
  bool hasAiStats = false;

  OptionPopup gameMenu;

  // Geometry helpers (9x9 board, portrait panel).
  int boardPitch() const;
  int boardOriginX() const;
  int boardOriginY() const;
  int stoneRadius() const;
  void intersectionXY(uint8_t r, uint8_t c, int* x, int* y) const;
  Rect boardTouchRect() const;
  int actionBarH() const;
  int actionBarY() const;
  Rect actionBarRect() const;
  // Left/right halves of the action bar: Place / Pass.
  Rect placeButtonRect() const;
  Rect passButtonRect() const;

  // Drawing
  void renderPlaying();
  void renderGameOver();
  void drawTitleBar();
  void drawBoard();
  void drawInfoPanel();
  void drawThinkingNotice();
  void drawFooter();
  void drawStone(int cx, int cy, int radius, bool isBlack) const;

  // Input
  void handleInputPlaying();
  void handleInputGameMenu();
  void handleInputGameOver();
  void enterGameMenu();
  void runMenuItem(uint8_t i);
  void resumeFromMenu();

  // Game flow
  void moveCursor(int dr, int dc);
  void startNewGame();
  void doPlace();
  void doPass();
  bool aiToMove();
  void runAiTurn();  // chooseMove → think → bestMove/pass; mutates game.
  void onGameOver(); // applies scoreDead/computeScore once, records stats.
  void scheduleSave();
  void flushSave();
  void pushMove(uint8_t idx);
  void undoMoves(uint8_t n);

  // Undo history (replayed on demand; not persisted across sessions).
  uint8_t moveLog[kMaxMoveLog] = {};
  uint16_t moveCount = 0;

  // Difficulty helpers (engine's DIFFS encoding).
  void applyHandicap();  // sets aiPlayer, handicap stones, komi per difficulty
};
