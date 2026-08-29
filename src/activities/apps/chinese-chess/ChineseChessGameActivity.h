#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "../GameSaveDebouncer.h"
#include "ChineseChessAI.h"
#include "ChineseChessBoard.h"
#include "ChineseChessStore.h"
#include "components/OptionPopup.h"

class ChineseChessGameActivity final : public Activity {
 public:
  ChineseChessGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ChineseChessMode mode, bool resume,
                           ChineseChessAiLevel aiLevel = ChineseChessAiLevel::Medium);
  ~ChineseChessGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, GameMenu, GameOver };

  // Portrait layout.
  static constexpr int CONTENT_X = 24;
  static constexpr int TITLE_BAR_H = 36;
  static constexpr int BOARD_AREA_Y = 60;
  static constexpr int BOARD_PITCH = 48;
  static constexpr int BOARD_ORIGIN_Y = BOARD_AREA_Y + 30;
  static constexpr int PIECE_RADIUS = 22;
  static constexpr int INFO_PANEL_Y = 602;
  static constexpr int INFO_PANEL_H = 60;
  static constexpr uint8_t MENU_ITEM_COUNT = 5;  // Resume / Undo / Resign / New Game / Exit

  // AI plays Black (player is Red, who moves first).
  static constexpr ChineseChessBoard::Side kAiSide = ChineseChessBoard::Side::Black;

  State state = State::Playing;
  ChineseChessMode mode = ChineseChessMode::TwoPlayer;
  ChineseChessAiLevel aiLevel = ChineseChessAiLevel::Medium;

  ChineseChessBoard board;
  uint8_t cursorR = 9;
  uint8_t cursorC = 4;
  uint8_t selR = ChineseChessBoard::INVALID_IDX;
  uint8_t selC = ChineseChessBoard::INVALID_IDX;
  bool hasSelection = false;
  ChineseChessBoard::Move legalCache[ChineseChessBoard::MAX_LEGAL_MOVES];
  uint8_t legalCount = 0;

  uint32_t redElapsedMs = 0;
  uint32_t blackElapsedMs = 0;
  uint32_t lastTickMs = 0;
  GameSaveDebouncer saveDebouncer;
  bool resumeRequested = false;
  bool aiThinkingArmed = false;
  bool aiThinkingShown = false;

  OptionPopup gameMenu;

  bool statsRecorded = false;
  bool resignedFlag = false;
  ChineseChessBoard::Side resignWinner = ChineseChessBoard::Side::Red;

  // Geometry helpers
  int boardOriginX() const;
  void cellXY(uint8_t r, uint8_t c, int* x, int* y) const;

  // Drawing
  void renderPlaying();
  void renderGameOver();
  void drawTitleBar();
  void drawBoard();
  void drawPieces();
  void drawCursor();
  void drawInfoPanel();
  void drawModeLine();
  void drawFooter();
  void drawPiece(int cx, int cy, uint8_t piece) const;
  void drawPalaceLines();
  void drawRiver();

  // Input
  void handleInputPlaying();
  void handleInputGameMenu();
  void handleInputGameOver();
  void enterGameMenu();
  void runMenuItem(uint8_t i);
  void resumeFromMenu();

  // Game flow
  void moveCursor(int dr, int dc);
  void doConfirm();
  void onGameOver();
  void scheduleSave();
  void flushSave();
  bool aiToMove() const;
  void runAiTurn();

  // Selection helpers
  void selectAt(uint8_t r, uint8_t c);
  void clearSelection();
  bool isLegalTarget(uint8_t r, uint8_t c) const;
};
