#pragma once

#include <cstdint>

struct Game2048SaveSlot {
  uint8_t cells[4][4] = {};  // exponents; 0 = empty
  uint32_t score = 0;
  bool won = false;          // any tile ever reached 2048 in this game
};

// Persists an in-progress 2048 game to /.crosspoint/2048.bin. Single-slot, same pattern as
// MinesweeperStore. `gameOver` is intentionally not stored — it's a pure function of the
// board and re-derived from Game2048Board::isStuck() on load.
class Game2048Store {
 public:
  static bool hasInProgress();
  static bool load(Game2048SaveSlot& out);
  static bool save(const Game2048SaveSlot& in);
  static bool clear();
};
