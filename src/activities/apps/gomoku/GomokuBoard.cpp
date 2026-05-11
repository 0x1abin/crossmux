#include "GomokuBoard.h"

#include <Logging.h>

#include <cstring>

void GomokuBoard::clear(uint8_t size) {
  if (size != 9 && size != 15) size = 15;
  boardSize = size;
  std::memset(cells, 0, sizeof(cells));
  std::memset(moveHistory, 0, sizeof(moveHistory));
  moveCount = 0;
  winner = Stone::Empty;
  winLineStart = INVALID_IDX;
  winLineEnd = INVALID_IDX;
}

GomokuBoard::Stone GomokuBoard::at(uint8_t r, uint8_t c) const {
  if (r >= boardSize || c >= boardSize) return Stone::Empty;
  return static_cast<Stone>(cells[idx(r, c)]);
}

bool GomokuBoard::isEmpty(uint8_t r, uint8_t c) const { return at(r, c) == Stone::Empty; }

uint16_t GomokuBoard::blackCount() const {
  uint16_t n = 0;
  const uint16_t total = static_cast<uint16_t>(boardSize) * boardSize;
  for (uint16_t i = 0; i < total; i++) {
    if (cells[i] == static_cast<uint8_t>(Stone::Black)) n++;
  }
  return n;
}

uint16_t GomokuBoard::whiteCount() const {
  uint16_t n = 0;
  const uint16_t total = static_cast<uint16_t>(boardSize) * boardSize;
  for (uint16_t i = 0; i < total; i++) {
    if (cells[i] == static_cast<uint8_t>(Stone::White)) n++;
  }
  return n;
}

bool GomokuBoard::placeStone(uint8_t r, uint8_t c) {
  if (winner != Stone::Empty) return false;
  if (r >= boardSize || c >= boardSize) return false;
  if (!isEmpty(r, c)) return false;

  const Stone color = nextTurn();
  cells[idx(r, c)] = static_cast<uint8_t>(color);
  moveHistory[moveCount++] = idx(r, c);

  if (detectWin(r, c, color)) {
    winner = color;
  }
  return true;
}

bool GomokuBoard::undo() {
  if (moveCount == 0) return false;
  const uint8_t lastIdx = moveHistory[moveCount - 1];
  cells[lastIdx] = static_cast<uint8_t>(Stone::Empty);
  moveCount--;
  // Undo always clears winner — winning move (if any) is the one being removed.
  winner = Stone::Empty;
  winLineStart = INVALID_IDX;
  winLineEnd = INVALID_IDX;
  return true;
}

bool GomokuBoard::detectWin(uint8_t r, uint8_t c, Stone color) {
  // Four line directions: horizontal, vertical, diag down-right, diag up-right.
  static constexpr int8_t kDir[4][2] = {{0, 1}, {1, 0}, {1, 1}, {-1, 1}};
  const uint8_t colorByte = static_cast<uint8_t>(color);
  for (int d = 0; d < 4; d++) {
    const int dr = kDir[d][0];
    const int dc = kDir[d][1];

    // Walk forward.
    int fwd = 0;
    for (int k = 1; k < WIN_LEN; k++) {
      const int rr = static_cast<int>(r) + dr * k;
      const int cc = static_cast<int>(c) + dc * k;
      if (rr < 0 || rr >= boardSize || cc < 0 || cc >= boardSize) break;
      if (cells[rr * boardSize + cc] != colorByte) break;
      fwd++;
    }
    // Walk backward.
    int bwd = 0;
    for (int k = 1; k < WIN_LEN; k++) {
      const int rr = static_cast<int>(r) - dr * k;
      const int cc = static_cast<int>(c) - dc * k;
      if (rr < 0 || rr >= boardSize || cc < 0 || cc >= boardSize) break;
      if (cells[rr * boardSize + cc] != colorByte) break;
      bwd++;
    }

    if (fwd + bwd + 1 >= WIN_LEN) {
      const int sr = static_cast<int>(r) - dr * bwd;
      const int sc = static_cast<int>(c) - dc * bwd;
      const int er = static_cast<int>(r) + dr * fwd;
      const int ec = static_cast<int>(c) + dc * fwd;
      winLineStart = static_cast<uint8_t>(sr * boardSize + sc);
      winLineEnd = static_cast<uint8_t>(er * boardSize + ec);
      return true;
    }
  }
  return false;
}

bool GomokuBoard::writeTo(HalFile& f) const {
  if (f.write(&boardSize, 1) != 1) return false;
  const uint8_t winnerByte = static_cast<uint8_t>(winner);
  if (f.write(&winnerByte, 1) != 1) return false;
  if (f.write(&winLineStart, 1) != 1) return false;
  if (f.write(&winLineEnd, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&moveCount), sizeof(moveCount)) != sizeof(moveCount)) return false;
  if (moveCount > 0) {
    if (f.write(moveHistory, moveCount) != static_cast<int>(moveCount)) return false;
  }
  return true;
}

bool GomokuBoard::readFrom(HalFile& f) {
  uint8_t bs = 0;
  if (f.read(&bs, 1) != 1) return false;
  if (bs != 9 && bs != 15) {
    LOG_ERR("GMK", "Save board size invalid: %u", static_cast<unsigned>(bs));
    return false;
  }
  uint8_t winnerByte = 0;
  if (f.read(&winnerByte, 1) != 1) return false;
  uint8_t ws = INVALID_IDX, we = INVALID_IDX;
  if (f.read(&ws, 1) != 1) return false;
  if (f.read(&we, 1) != 1) return false;
  uint16_t mc = 0;
  if (f.read(reinterpret_cast<uint8_t*>(&mc), sizeof(mc)) != sizeof(mc)) return false;
  if (mc > MAX_CELLS) {
    LOG_ERR("GMK", "Save move count out of range: %u", static_cast<unsigned>(mc));
    return false;
  }

  clear(bs);
  if (mc > 0) {
    if (f.read(moveHistory, mc) != static_cast<int>(mc)) return false;
  }
  // Reconstruct cells[] from moveHistory; alternating colors starting Black.
  for (uint16_t i = 0; i < mc; i++) {
    const uint8_t cellIdx = moveHistory[i];
    if (cellIdx >= boardSize * boardSize) {
      LOG_ERR("GMK", "Save move index out of range");
      return false;
    }
    cells[cellIdx] = static_cast<uint8_t>((i % 2 == 0) ? Stone::Black : Stone::White);
  }
  moveCount = mc;
  winner = static_cast<Stone>(winnerByte);
  winLineStart = ws;
  winLineEnd = we;
  return true;
}
