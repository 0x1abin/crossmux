#pragma once

#include <cstdint>

#include "GomokuBoard.h"

// Stats are tracked per board size: index 0 = 15×15, index 1 = 9×9.
struct GomokuStats {
  uint16_t bestTimeSec[2] = {0, 0};  // 0 = no record
  uint16_t blackWins[2] = {0, 0};
  uint16_t whiteWins[2] = {0, 0};
  uint16_t draws[2] = {0, 0};
  uint16_t startedCount[2] = {0, 0};  // cumulative started games
};

enum class GomokuMode : uint8_t { TwoPlayer = 0, VsAi = 1 };

struct GomokuSaveSlot {
  GomokuBoard board;
  GomokuMode mode = GomokuMode::TwoPlayer;
  uint8_t aiDifficulty = 1;  // reserved for phase B2
  uint8_t cursorR = 7;
  uint8_t cursorC = 7;
  uint16_t elapsedSec = 0;
};

class GomokuStore {
 public:
  // In-progress save (single slot)
  static bool hasInProgress();
  static bool load(GomokuSaveSlot& out);
  static bool save(const GomokuSaveSlot& in);
  static bool clear();

  // Cumulative stats
  static GomokuStats loadStats();
  static bool saveStats(const GomokuStats& stats);
  static void recordStart(uint8_t boardSize);
  static void recordWin(uint8_t boardSize, GomokuBoard::Stone winner, uint16_t timeSec);
  static void recordDraw(uint8_t boardSize);

  // Helper: 0 = 15×15, 1 = 9×9, 0xFF = invalid.
  static uint8_t sizeIndex(uint8_t boardSize) {
    if (boardSize == 15) return 0;
    if (boardSize == 9) return 1;
    return 0xFF;
  }
};
