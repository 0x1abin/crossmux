#pragma once

#include <HalStorage.h>

#include <cstdint>

class MinesweeperBoard {
 public:
  enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };
  enum CellState : uint8_t { Hidden = 0, Revealed = 1, Flagged = 2 };

  // One byte per cell. The 16×16 array always materialises in DRAM; smaller
  // difficulties leave the trailing cells zeroed. 256 B total fits the resource
  // budget and lets the save format use a fixed cell-array length.
  struct Cell {
    uint8_t hasMine : 1;
    uint8_t state : 2;          // CellState
    uint8_t neighborMines : 4;  // 0..8
    uint8_t mineExploded : 1;   // set on the cell the player stepped on
  };

  static constexpr uint8_t MAX_SIDE = 16;
  static constexpr uint16_t MAX_CELLS = static_cast<uint16_t>(MAX_SIDE) * MAX_SIDE;  // 256

  Cell cells[MAX_CELLS] = {};

  uint8_t rows = 0;
  uint8_t cols = 0;
  uint16_t mineCount = 0;
  uint16_t flagsPlaced = 0;
  uint16_t revealedCount = 0;
  bool minesPlaced = false;
  bool exploded = false;

  static void difficultyDims(Difficulty d, uint8_t& rows, uint8_t& cols, uint16_t& mines);

  // Clear board and configure dimensions for the given difficulty. Does not
  // place mines — mines are placed lazily on the first dig() so the first
  // click is always safe.
  void init(Difficulty d);

  // Reset only the revealed/flagged state — keeps the same mine layout. Used
  // by the "Restart" menu item.
  void resetForRestart();

  Cell& at(uint8_t r, uint8_t c) { return cells[r * cols + c]; }
  const Cell& at(uint8_t r, uint8_t c) const { return cells[r * cols + c]; }
  bool inBounds(int r, int c) const { return r >= 0 && c >= 0 && r < rows && c < cols; }

  // Reveal (r,c) and its 0-neighbour cascade. No-op when the cell is already
  // revealed or flagged. Returns true if the player stepped on a mine.
  bool dig(uint8_t r, uint8_t c);

  // Toggle the flag on a hidden cell. No-op when the cell is already revealed.
  void toggleFlag(uint8_t r, uint8_t c);

  // Reveal every mine on the board (used on Lost state to expose the layout).
  void revealAllMines();

  uint16_t totalCells() const { return static_cast<uint16_t>(rows) * cols; }
  bool isWon() const { return revealedCount + mineCount == totalCells(); }

  // Fixed-size binary form:
  //   1B rows, 1B cols, 2B mineCount, 2B flagsPlaced, 2B revealedCount,
  //   1B minesPlaced, 1B exploded, MAX_CELLS B packed cell bytes.
  bool writeTo(HalFile& f) const;
  bool readFrom(HalFile& f);

 private:
  // Iterative BFS-style cascade for 0-neighbour cells.
  void revealCascade(uint8_t r, uint8_t c);
};
