#include "MinesweeperGenerator.h"

#include <esp_random.h>

#include "MinesweeperBoard.h"

void MinesweeperGenerator::placeMines(MinesweeperBoard& board, uint8_t safeR, uint8_t safeC) {
  // Build a candidate pool of cell indices outside the 3×3 safe zone around
  // (safeR, safeC). The pool fits in 256 B regardless of difficulty.
  uint8_t pool[MinesweeperBoard::MAX_CELLS];
  uint16_t poolSize = 0;
  for (uint8_t r = 0; r < board.rows; r++) {
    for (uint8_t c = 0; c < board.cols; c++) {
      const int dr = static_cast<int>(r) - static_cast<int>(safeR);
      const int dc = static_cast<int>(c) - static_cast<int>(safeC);
      const bool inSafeZone = (dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1);
      if (inSafeZone) continue;
      pool[poolSize++] = static_cast<uint8_t>(r * board.cols + c);
    }
  }

  // Defensive: in pathological cases (every cell except the safe zone is a
  // mine) clamp so we don't sample more than the pool can yield.
  uint16_t toPlace = board.mineCount;
  if (toPlace > poolSize) toPlace = poolSize;

  // Fisher-Yates partial shuffle: pick the first `toPlace` slots uniformly.
  for (uint16_t i = 0; i < toPlace; i++) {
    const uint16_t span = static_cast<uint16_t>(poolSize - i);
    const uint16_t j = static_cast<uint16_t>(i + (esp_random() % span));
    const uint8_t tmp = pool[i];
    pool[i] = pool[j];
    pool[j] = tmp;
    board.cells[pool[i]].hasMine = 1;
  }

  // Recompute neighbor counts now that the mine layout is finalised.
  for (uint8_t r = 0; r < board.rows; r++) {
    for (uint8_t c = 0; c < board.cols; c++) {
      MinesweeperBoard::Cell& cell = board.at(r, c);
      if (cell.hasMine) {
        cell.neighborMines = 0;
        continue;
      }
      uint8_t n = 0;
      for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
          if (dr == 0 && dc == 0) continue;
          const int nr = static_cast<int>(r) + dr;
          const int nc = static_cast<int>(c) + dc;
          if (!board.inBounds(nr, nc)) continue;
          if (board.at(static_cast<uint8_t>(nr), static_cast<uint8_t>(nc)).hasMine) n++;
        }
      }
      cell.neighborMines = n;
    }
  }

  board.minesPlaced = true;
}
