#pragma once

#include <cstdint>

// 2048 game board: 4×4 grid of tiles stored as power-of-two exponents.
// 0 = empty, 1 = "2", 2 = "4", ..., 11 = "2048", 16 = "65536" (theoretical max).
class Game2048Board {
 public:
  static constexpr int SIZE = 4;
  static constexpr uint8_t WIN_EXPONENT = 11;  // 2^11 = 2048

  enum class Direction : uint8_t { Up, Down, Left, Right };

  void reset();

  // Slide all tiles toward `d`, merging adjacent equal pairs (each tile merges at most once
  // per move). Returns true iff anything moved or merged. `scoreDelta` is incremented by the
  // sum of merged tile values produced this move. On a true return, also spawns one new tile.
  bool slide(Direction d, uint32_t& scoreDelta);

  bool isWon() const;    // any tile ≥ WIN_EXPONENT
  bool isStuck() const;  // no empty cells AND no merges possible in any direction

  uint8_t at(int r, int c) const { return cells_[r][c]; }
  void set(int r, int c, uint8_t exponent) { cells_[r][c] = exponent; }

 private:
  uint8_t cells_[SIZE][SIZE] = {};

  // Slide one length-SIZE column/row toward index 0, merging once. Returns true if changed.
  static bool slideLine(uint8_t* line, uint32_t& scoreDelta);

  // Pick a random empty cell, place 1 (=2) with 90% probability, else 2 (=4).
  void spawn();
};
