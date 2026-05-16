#pragma once

#include <cstdint>

#include "MinesweeperBoard.h"

struct MinesweeperStats {
  uint16_t bestTimeSec[3] = {0, 0, 0};  // 0 = no record, indexed by Difficulty
  uint16_t completedCount[3] = {0, 0, 0};
  uint16_t startedCount[3] = {0, 0, 0};
};

struct MinesweeperSaveSlot {
  MinesweeperBoard board;
  MinesweeperBoard::Difficulty difficulty = MinesweeperBoard::Difficulty::Easy;
  uint16_t elapsedSec = 0;
  uint8_t cursorR = 0;
  uint8_t cursorC = 0;
  bool flagMode = false;
  uint8_t hintsLeft = 3;
};

class MinesweeperStore {
 public:
  // In-progress game save (single slot)
  static bool hasInProgress();
  static bool load(MinesweeperSaveSlot& out);
  static bool save(const MinesweeperSaveSlot& in);
  static bool clear();

  // Cumulative stats per difficulty
  static MinesweeperStats loadStats();
  static bool saveStats(const MinesweeperStats& stats);
  static void recordStart(MinesweeperBoard::Difficulty d);
  static void recordCompletion(MinesweeperBoard::Difficulty d, uint16_t timeSec);
};
