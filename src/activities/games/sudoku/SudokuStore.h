#pragma once

#include <cstdint>

#include "SudokuBoard.h"

struct SudokuStats {
  uint16_t bestTimeSec[3] = {0, 0, 0};     // 0 = no record
  uint16_t completedCount[3] = {0, 0, 0};  // total wins per difficulty
  uint16_t startedCount[3] = {0, 0, 0};    // total games started (for win rate)
};

struct SudokuSaveSlot {
  SudokuBoard board;
  SudokuBoard::Difficulty difficulty = SudokuBoard::Difficulty::Easy;
  uint16_t elapsedSec = 0;
  uint8_t cursorR = 4;
  uint8_t cursorC = 4;
  bool notesMode = false;
  uint8_t mistakes = 0;
  uint8_t hintsLeft = 3;
};

class SudokuStore {
 public:
  // In-progress puzzle save (single slot)
  static bool hasInProgress();
  static bool load(SudokuSaveSlot& out);
  static bool save(const SudokuSaveSlot& in);
  static bool clear();

  // Cumulative stats
  static SudokuStats loadStats();
  static bool saveStats(const SudokuStats& stats);
  static void recordStart(SudokuBoard::Difficulty d);
  static void recordCompletion(SudokuBoard::Difficulty d, uint16_t timeSec);
};
