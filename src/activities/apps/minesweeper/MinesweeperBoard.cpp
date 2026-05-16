#include "MinesweeperBoard.h"

#include <Logging.h>

#include <cstring>

void MinesweeperBoard::difficultyDims(Difficulty d, uint8_t& rowsOut, uint8_t& colsOut, uint16_t& minesOut) {
  switch (d) {
    case Difficulty::Easy:
      rowsOut = 9;
      colsOut = 9;
      minesOut = 10;
      return;
    case Difficulty::Medium:
      rowsOut = 12;
      colsOut = 12;
      minesOut = 24;
      return;
    case Difficulty::Hard:
      rowsOut = 16;
      colsOut = 16;
      minesOut = 50;
      return;
  }
}

void MinesweeperBoard::init(Difficulty d) {
  std::memset(cells, 0, sizeof(cells));
  difficultyDims(d, rows, cols, mineCount);
  flagsPlaced = 0;
  revealedCount = 0;
  minesPlaced = false;
  exploded = false;
}

void MinesweeperBoard::resetForRestart() {
  for (uint16_t i = 0; i < MAX_CELLS; i++) {
    cells[i].state = Hidden;
    cells[i].mineExploded = 0;
  }
  flagsPlaced = 0;
  revealedCount = 0;
  exploded = false;
}

bool MinesweeperBoard::dig(uint8_t r, uint8_t c) {
  if (!inBounds(r, c)) return false;
  Cell& cell = at(r, c);
  if (cell.state == Revealed || cell.state == Flagged) return false;

  if (cell.hasMine) {
    cell.state = Revealed;
    cell.mineExploded = 1;
    exploded = true;
    return true;
  }

  cell.state = Revealed;
  revealedCount++;
  if (cell.neighborMines == 0) {
    revealCascade(r, c);
  }
  return false;
}

void MinesweeperBoard::revealCascade(uint8_t r, uint8_t c) {
  // Iterative BFS-style flood. 256-byte stack fits comfortably in task stack.
  uint8_t stack[MAX_CELLS];
  int sp = 0;
  stack[sp++] = static_cast<uint8_t>(r * cols + c);
  while (sp > 0) {
    const uint8_t idx = stack[--sp];
    const uint8_t cr = idx / cols;
    const uint8_t cc = idx % cols;
    for (int dr = -1; dr <= 1; dr++) {
      for (int dc = -1; dc <= 1; dc++) {
        if (dr == 0 && dc == 0) continue;
        const int nr = cr + dr;
        const int nc = cc + dc;
        if (!inBounds(nr, nc)) continue;
        Cell& n = at(static_cast<uint8_t>(nr), static_cast<uint8_t>(nc));
        if (n.state != Hidden) continue;
        if (n.hasMine) continue;
        n.state = Revealed;
        revealedCount++;
        if (n.neighborMines == 0 && sp < static_cast<int>(MAX_CELLS)) {
          stack[sp++] = static_cast<uint8_t>(nr * cols + nc);
        }
      }
    }
  }
}

void MinesweeperBoard::toggleFlag(uint8_t r, uint8_t c) {
  if (!inBounds(r, c)) return;
  Cell& cell = at(r, c);
  if (cell.state == Revealed) return;
  if (cell.state == Flagged) {
    cell.state = Hidden;
    if (flagsPlaced > 0) flagsPlaced--;
  } else {
    cell.state = Flagged;
    flagsPlaced++;
  }
}

void MinesweeperBoard::revealAllMines() {
  for (uint8_t r = 0; r < rows; r++) {
    for (uint8_t c = 0; c < cols; c++) {
      Cell& cell = at(r, c);
      if (cell.hasMine && cell.state != Revealed) {
        cell.state = Revealed;
      }
    }
  }
}

namespace {
constexpr uint8_t SERIALIZED_HEADER_BYTES =
    10;  // rows+cols+mineCount(2)+flagsPlaced(2)+revealedCount(2)+minesPlaced+exploded
}

bool MinesweeperBoard::writeTo(HalFile& f) const {
  uint8_t header[SERIALIZED_HEADER_BYTES];
  header[0] = rows;
  header[1] = cols;
  header[2] = static_cast<uint8_t>(mineCount & 0xFF);
  header[3] = static_cast<uint8_t>((mineCount >> 8) & 0xFF);
  header[4] = static_cast<uint8_t>(flagsPlaced & 0xFF);
  header[5] = static_cast<uint8_t>((flagsPlaced >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>(revealedCount & 0xFF);
  header[7] = static_cast<uint8_t>((revealedCount >> 8) & 0xFF);
  header[8] = minesPlaced ? 1 : 0;
  header[9] = exploded ? 1 : 0;
  if (f.write(header, sizeof(header)) != sizeof(header)) return false;
  // Cells are 1 byte each thanks to bitfield packing; verify at compile time.
  static_assert(sizeof(Cell) == 1, "MinesweeperBoard::Cell must be 1 byte");
  if (f.write(cells, sizeof(cells)) != sizeof(cells)) return false;
  return true;
}

bool MinesweeperBoard::readFrom(HalFile& f) {
  uint8_t header[SERIALIZED_HEADER_BYTES];
  if (f.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return false;
  rows = header[0];
  cols = header[1];
  if (rows == 0 || cols == 0 || rows > MAX_SIDE || cols > MAX_SIDE) {
    LOG_ERR("MSW", "Invalid board dims %ux%u", rows, cols);
    return false;
  }
  mineCount = static_cast<uint16_t>(header[2]) | (static_cast<uint16_t>(header[3]) << 8);
  flagsPlaced = static_cast<uint16_t>(header[4]) | (static_cast<uint16_t>(header[5]) << 8);
  revealedCount = static_cast<uint16_t>(header[6]) | (static_cast<uint16_t>(header[7]) << 8);
  minesPlaced = header[8] != 0;
  exploded = header[9] != 0;
  if (f.read(cells, sizeof(cells)) != static_cast<int>(sizeof(cells))) return false;
  return true;
}
