#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "../GameSaveDebouncer.h"
#include "SokobanBoard.h"
#include "SokobanStore.h"
#include "levels_data.h"

class SokobanGameActivity final : public Activity {
 public:
  SokobanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~SokobanGameActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Playing, Won, LevelSelect };

  State state = State::Playing;
  SokobanBoard board;
  int currentLevel = 0;
  int moves = 0;
  GameSaveDebouncer saveDebouncer;

  static constexpr uint32_t kInitialHoldDelayMs = 350;
  static constexpr uint32_t kRepeatMoveIntervalMs = 220;
  uint32_t lastCursorMoveTime = 0;
  bool isFirstMoveAfterHold = false;
  int heldDr = 0, heldDc = 0;

  uint32_t lastLevelSelectScrollTime = 0;
  bool isFirstLevelSelectHold = false;
  int heldLevelSelectDir = 0;  // 1向下，-1向上

  int selectedLevel = 0;
  int scrollOffset = 0;

  void handleInput();
  void move(int dr, int dc);
  void undo();
  void resetLevel();
  void loadLevel(int idx);
  void onWin();
  void scheduleSave();
  void flushSave();

  void drawBoard();
  void drawHUD();
  void drawFooter();
  void drawWinScreen();
  void drawLevelSelect();

  static constexpr int MAX_LEVELS = 160;
  const uint8_t* builtinLevels = kLevelsBin;
  size_t builtinLevelsSize = kLevelsBinSize;
  int totalLevels = 0;
  uint32_t levelOffsets[MAX_LEVELS];
};
