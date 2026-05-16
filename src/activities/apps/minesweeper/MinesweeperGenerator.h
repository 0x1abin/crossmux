#pragma once

#include <cstdint>

class MinesweeperBoard;

class MinesweeperGenerator {
 public:
  // Place `board.mineCount` mines on the board, avoiding the 3×3 region
  // centred on (safeR, safeC). The caller must call this exactly once,
  // before the first dig — typical Minesweeper rule that the player's first
  // click is always safe. Sets `board.minesPlaced = true` on success and
  // populates each non-mine cell's neighborMines count.
  static void placeMines(MinesweeperBoard& board, uint8_t safeR, uint8_t safeC);
};
