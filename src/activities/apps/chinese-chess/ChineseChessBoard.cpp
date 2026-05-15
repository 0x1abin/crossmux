#include "ChineseChessBoard.h"

#include <Logging.h>

#include <cstring>

namespace {

// Initial position. Index = r * FILES + c. Red is at ranks 7..9 (bottom),
// Black is at ranks 0..2 (top). Pawns at rank 3 (Black) and rank 6 (Red).
// Cannons at rank 2 / rank 7.
constexpr uint8_t kInitial[ChineseChessBoard::CELLS] = {
    // rank 0 (Black back rank): R H E A K A E H R
    ChineseChessBoard::BlackChariot,
    ChineseChessBoard::BlackHorse,
    ChineseChessBoard::BlackElephant,
    ChineseChessBoard::BlackAdvisor,
    ChineseChessBoard::BlackKing,
    ChineseChessBoard::BlackAdvisor,
    ChineseChessBoard::BlackElephant,
    ChineseChessBoard::BlackHorse,
    ChineseChessBoard::BlackChariot,
    // rank 1 (empty)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    // rank 2 (cannons at file 1 and 7)
    0,
    ChineseChessBoard::BlackCannon,
    0,
    0,
    0,
    0,
    0,
    ChineseChessBoard::BlackCannon,
    0,
    // rank 3 (Black pawns at files 0,2,4,6,8)
    ChineseChessBoard::BlackPawn,
    0,
    ChineseChessBoard::BlackPawn,
    0,
    ChineseChessBoard::BlackPawn,
    0,
    ChineseChessBoard::BlackPawn,
    0,
    ChineseChessBoard::BlackPawn,
    // rank 4 (empty / river top)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    // rank 5 (empty / river bottom)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    // rank 6 (Red pawns at files 0,2,4,6,8)
    ChineseChessBoard::RedPawn,
    0,
    ChineseChessBoard::RedPawn,
    0,
    ChineseChessBoard::RedPawn,
    0,
    ChineseChessBoard::RedPawn,
    0,
    ChineseChessBoard::RedPawn,
    // rank 7 (cannons at file 1 and 7)
    0,
    ChineseChessBoard::RedCannon,
    0,
    0,
    0,
    0,
    0,
    ChineseChessBoard::RedCannon,
    0,
    // rank 8 (empty)
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    // rank 9 (Red back rank)
    ChineseChessBoard::RedChariot,
    ChineseChessBoard::RedHorse,
    ChineseChessBoard::RedElephant,
    ChineseChessBoard::RedAdvisor,
    ChineseChessBoard::RedKing,
    ChineseChessBoard::RedAdvisor,
    ChineseChessBoard::RedElephant,
    ChineseChessBoard::RedHorse,
    ChineseChessBoard::RedChariot,
};

inline void appendMove(ChineseChessBoard::Move* out, uint8_t& n, uint8_t outCap, uint8_t from, uint8_t to,
                       uint8_t captured) {
  if (n >= outCap) return;
  out[n].from = from;
  out[n].to = to;
  out[n].captured = captured;
  n++;
}

// Compile-time Zobrist table. splitmix64 is a well-mixed 64-bit PRNG that is
// constexpr-evaluable here, so the table lives entirely in flash with zero
// init cost. Fixed seed → reproducible hashes across builds (helps tests).
constexpr uint64_t splitmix64Next(uint64_t& s) {
  s += 0x9E3779B97F4A7C15ULL;
  uint64_t z = s;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

struct ZobristTable {
  // [piece-encoding 0..15][cell index 0..89]. Rows 0 and 8 are unused (empty /
  // unused side bit) but we keep the [16][90] shape so we can index directly
  // by raw cell value without a translation step. Wasted: 2*90*8 = 1440 bytes
  // of flash, acceptable.
  uint64_t piece[16][ChineseChessBoard::CELLS];
  uint64_t blackToMove;
};

constexpr ZobristTable makeZobristTable() {
  ZobristTable z{};
  uint64_t s = 0x00C0FFEE12345678ULL;
  for (uint8_t p = 0; p < 16; ++p) {
    for (uint8_t i = 0; i < ChineseChessBoard::CELLS; ++i) {
      z.piece[p][i] = splitmix64Next(s);
    }
  }
  z.blackToMove = splitmix64Next(s);
  return z;
}

constexpr ZobristTable kZobrist = makeZobristTable();

}  // namespace

void ChineseChessBoard::reset() {
  std::memcpy(cells, kInitial, sizeof(cells));
  std::memset(moveHistory, 0, sizeof(moveHistory));
  moveCount = 0;
  result = Result::Ongoing;
  resigned = false;
  recomputeZobrist();
}

uint8_t ChineseChessBoard::at(uint8_t r, uint8_t c) const {
  if (r >= RANKS || c >= FILES) return Empty;
  return cells[idx(r, c)];
}

uint8_t ChineseChessBoard::findKing(Side side) const {
  const uint8_t target = makePiece(side, Kind::King);
  for (uint8_t i = 0; i < CELLS; i++) {
    if (cells[i] == target) return i;
  }
  return INVALID_IDX;
}

// ============== Move generation ==============

void ChineseChessBoard::genKing(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  static constexpr int8_t kDir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (int d = 0; d < 4; d++) {
    const int rr = r + kDir[d][0];
    const int cc = c + kDir[d][1];
    if (!inBounds(rr, cc)) continue;
    if (!inPalace(s, static_cast<uint8_t>(rr), static_cast<uint8_t>(cc))) continue;
    const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
    const uint8_t v = cells[dst];
    if (v != Empty && sideOf(v) == s) continue;
    appendMove(out, n, outCap, from, dst, v);
  }
}

void ChineseChessBoard::genAdvisor(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  static constexpr int8_t kDir[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
  for (int d = 0; d < 4; d++) {
    const int rr = r + kDir[d][0];
    const int cc = c + kDir[d][1];
    if (!inBounds(rr, cc)) continue;
    if (!inPalace(s, static_cast<uint8_t>(rr), static_cast<uint8_t>(cc))) continue;
    const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
    const uint8_t v = cells[dst];
    if (v != Empty && sideOf(v) == s) continue;
    appendMove(out, n, outCap, from, dst, v);
  }
}

void ChineseChessBoard::genElephant(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  // Four diagonal moves of 2; midpoint must be empty; cannot cross river.
  static constexpr int8_t kDir[4][2] = {{-2, -2}, {-2, 2}, {2, -2}, {2, 2}};
  for (int d = 0; d < 4; d++) {
    const int rr = r + kDir[d][0];
    const int cc = c + kDir[d][1];
    if (!inBounds(rr, cc)) continue;
    if (!inOwnHalf(s, static_cast<uint8_t>(rr))) continue;
    const int mr = r + kDir[d][0] / 2;
    const int mc = c + kDir[d][1] / 2;
    if (cells[idx(static_cast<uint8_t>(mr), static_cast<uint8_t>(mc))] != Empty) continue;  // 塞象眼
    const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
    const uint8_t v = cells[dst];
    if (v != Empty && sideOf(v) == s) continue;
    appendMove(out, n, outCap, from, dst, v);
  }
}

void ChineseChessBoard::genHorse(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  // Eight L moves; the orthogonal cell adjacent to the horse must be empty (蹩马腿).
  // {dr, dc, leg_dr, leg_dc}
  static constexpr int8_t kMv[8][4] = {
      {-2, -1, -1, 0}, {-2, 1, -1, 0}, {2, -1, 1, 0}, {2, 1, 1, 0},
      {-1, -2, 0, -1}, {1, -2, 0, -1}, {-1, 2, 0, 1}, {1, 2, 0, 1},
  };
  for (int d = 0; d < 8; d++) {
    const int rr = r + kMv[d][0];
    const int cc = c + kMv[d][1];
    if (!inBounds(rr, cc)) continue;
    const int lr = r + kMv[d][2];
    const int lc = c + kMv[d][3];
    if (cells[idx(static_cast<uint8_t>(lr), static_cast<uint8_t>(lc))] != Empty) continue;
    const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
    const uint8_t v = cells[dst];
    if (v != Empty && sideOf(v) == s) continue;
    appendMove(out, n, outCap, from, dst, v);
  }
}

void ChineseChessBoard::genChariot(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  static constexpr int8_t kDir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (int d = 0; d < 4; d++) {
    int rr = r, cc = c;
    while (true) {
      rr += kDir[d][0];
      cc += kDir[d][1];
      if (!inBounds(rr, cc)) break;
      const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
      const uint8_t v = cells[dst];
      if (v == Empty) {
        appendMove(out, n, outCap, from, dst, 0);
      } else {
        if (sideOf(v) != s) appendMove(out, n, outCap, from, dst, v);
        break;
      }
    }
  }
}

void ChineseChessBoard::genCannon(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  static constexpr int8_t kDir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
  for (int d = 0; d < 4; d++) {
    int rr = r, cc = c;
    bool jumped = false;  // After the screen piece, only a capture move is legal.
    while (true) {
      rr += kDir[d][0];
      cc += kDir[d][1];
      if (!inBounds(rr, cc)) break;
      const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(cc));
      const uint8_t v = cells[dst];
      if (!jumped) {
        if (v == Empty) {
          appendMove(out, n, outCap, from, dst, 0);
        } else {
          jumped = true;
        }
      } else {
        if (v == Empty) continue;
        if (sideOf(v) != s) appendMove(out, n, outCap, from, dst, v);
        break;
      }
    }
  }
}

void ChineseChessBoard::genPawn(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const {
  const Side s = sideOf(cells[from]);
  const int r = rowOf(from), c = colOf(from);
  // Red pawns move up (decreasing rank), Black pawns move down.
  const int forward = (s == Side::Red) ? -1 : +1;
  // 1) Forward.
  {
    const int rr = r + forward;
    if (inBounds(rr, c)) {
      const uint8_t dst = idx(static_cast<uint8_t>(rr), static_cast<uint8_t>(c));
      const uint8_t v = cells[dst];
      if (v == Empty || sideOf(v) != s) appendMove(out, n, outCap, from, dst, v);
    }
  }
  // 2) Sideways, only after crossing the river.
  const bool crossed = (s == Side::Red) ? (r <= 4) : (r >= 5);
  if (crossed) {
    static constexpr int8_t kDir[2] = {-1, 1};
    for (int d = 0; d < 2; d++) {
      const int cc = c + kDir[d];
      if (!inBounds(r, cc)) continue;
      const uint8_t dst = idx(static_cast<uint8_t>(r), static_cast<uint8_t>(cc));
      const uint8_t v = cells[dst];
      if (v == Empty || sideOf(v) != s) appendMove(out, n, outCap, from, dst, v);
    }
  }
}

uint8_t ChineseChessBoard::generatePseudoMoves(Side side, Move* out, uint8_t outCap) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < CELLS; i++) {
    const uint8_t v = cells[i];
    if (v == Empty) continue;
    if (sideOf(v) != side) continue;
    switch (kindOf(v)) {
      case Kind::King:
        genKing(i, out, n, outCap);
        break;
      case Kind::Advisor:
        genAdvisor(i, out, n, outCap);
        break;
      case Kind::Elephant:
        genElephant(i, out, n, outCap);
        break;
      case Kind::Horse:
        genHorse(i, out, n, outCap);
        break;
      case Kind::Chariot:
        genChariot(i, out, n, outCap);
        break;
      case Kind::Cannon:
        genCannon(i, out, n, outCap);
        break;
      case Kind::Pawn:
        genPawn(i, out, n, outCap);
        break;
    }
  }
  return n;
}

bool ChineseChessBoard::kingsFacing() const {
  const uint8_t rk = findKing(Side::Red);
  const uint8_t bk = findKing(Side::Black);
  if (rk == INVALID_IDX || bk == INVALID_IDX) return false;
  if (colOf(rk) != colOf(bk)) return false;
  const uint8_t c = colOf(rk);
  const uint8_t lo = (rowOf(rk) < rowOf(bk)) ? rowOf(rk) : rowOf(bk);
  const uint8_t hi = (rowOf(rk) < rowOf(bk)) ? rowOf(bk) : rowOf(rk);
  for (uint8_t r = lo + 1; r < hi; r++) {
    if (cells[idx(r, c)] != Empty) return false;
  }
  return true;
}

bool ChineseChessBoard::inCheck(Side side) const {
  // Check 1: king-facing rule (treat as own king "in check" so the move is illegal).
  if (kingsFacing()) return true;
  // Check 2: any pseudo move by opponent that captures own king.
  const uint8_t kingPos = findKing(side);
  if (kingPos == INVALID_IDX) return true;  // king missing = effectively checkmate
  Move tmp[MAX_LEGAL_MOVES];
  const Side opp = (side == Side::Red) ? Side::Black : Side::Red;
  const uint8_t n = generatePseudoMoves(opp, tmp, MAX_LEGAL_MOVES);
  for (uint8_t i = 0; i < n; i++) {
    if (tmp[i].to == kingPos) return true;
  }
  return false;
}

uint8_t ChineseChessBoard::generateLegalMoves(Side side, Move* out, uint8_t outCap) const {
  Move pseudo[MAX_LEGAL_MOVES];
  const uint8_t total = generatePseudoMoves(side, pseudo, MAX_LEGAL_MOVES);
  uint8_t n = 0;
  for (uint8_t i = 0; i < total; i++) {
    if (n >= outCap) break;
    // Make-undo trial on a mutable copy. Intentionally bypasses makeMove/undo:
    // inCheck() does not read zobristHash, so leaving the hash unmodified
    // during the trial is safe and avoids a redundant XOR.
    ChineseChessBoard* self = const_cast<ChineseChessBoard*>(this);
    const uint8_t fromV = cells[pseudo[i].from];
    const uint8_t toV = cells[pseudo[i].to];
    self->cells[pseudo[i].to] = fromV;
    self->cells[pseudo[i].from] = Empty;
    const bool legal = !inCheck(side);
    self->cells[pseudo[i].from] = fromV;
    self->cells[pseudo[i].to] = toV;
    if (legal) out[n++] = pseudo[i];
  }
  return n;
}

uint8_t ChineseChessBoard::generateLegalMovesFrom(uint8_t from, Move* out, uint8_t outCap) const {
  if (from >= CELLS) return 0;
  const uint8_t v = cells[from];
  if (v == Empty) return 0;
  Move pseudo[MAX_LEGAL_MOVES];
  uint8_t pn = 0;
  switch (kindOf(v)) {
    case Kind::King:
      genKing(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Advisor:
      genAdvisor(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Elephant:
      genElephant(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Horse:
      genHorse(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Chariot:
      genChariot(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Cannon:
      genCannon(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
    case Kind::Pawn:
      genPawn(from, pseudo, pn, MAX_LEGAL_MOVES);
      break;
  }
  const Side s = sideOf(v);
  uint8_t n = 0;
  for (uint8_t i = 0; i < pn; i++) {
    if (n >= outCap) break;
    ChineseChessBoard* self = const_cast<ChineseChessBoard*>(this);
    const uint8_t fromV = cells[pseudo[i].from];
    const uint8_t toV = cells[pseudo[i].to];
    self->cells[pseudo[i].to] = fromV;
    self->cells[pseudo[i].from] = Empty;
    const bool legal = !inCheck(s);
    self->cells[pseudo[i].from] = fromV;
    self->cells[pseudo[i].to] = toV;
    if (legal) out[n++] = pseudo[i];
  }
  return n;
}

bool ChineseChessBoard::makeMove(const Move& m) {
  if (m.from >= CELLS || m.to >= CELLS) return false;
  const uint8_t mover = cells[m.from];
  if (mover == Empty) return false;
  Move stored = m;
  stored.captured = cells[m.to];
  cells[m.to] = mover;
  cells[m.from] = Empty;
  // Incremental Zobrist update: remove mover from origin, remove captured (if
  // any) from destination, place mover at destination, flip side-to-move.
  zobristHash ^= kZobrist.piece[mover][m.from];
  if (stored.captured) zobristHash ^= kZobrist.piece[stored.captured][m.to];
  zobristHash ^= kZobrist.piece[mover][m.to];
  zobristHash ^= kZobrist.blackToMove;
  if (moveCount < MAX_MOVES) {
    moveHistory[moveCount++] = stored;
  } else {
    LOG_ERR("XQI", "moveHistory overflow at %u", static_cast<unsigned>(moveCount));
  }
  return true;
}

bool ChineseChessBoard::undo() {
  if (moveCount == 0) return false;
  moveCount--;
  const Move& m = moveHistory[moveCount];
  if (m.from >= CELLS || m.to >= CELLS) return false;
  const uint8_t mover = cells[m.to];  // piece sitting at destination is the mover
  cells[m.from] = mover;
  cells[m.to] = m.captured;
  // XOR is its own inverse; same four operations as makeMove.
  zobristHash ^= kZobrist.piece[mover][m.from];
  if (m.captured) zobristHash ^= kZobrist.piece[m.captured][m.to];
  zobristHash ^= kZobrist.piece[mover][m.to];
  zobristHash ^= kZobrist.blackToMove;
  result = Result::Ongoing;
  resigned = false;
  return true;
}

bool ChineseChessBoard::givesCheck(Side mover, const Move& m) const {
  if (m.from >= CELLS || m.to >= CELLS) return false;
  ChineseChessBoard* self = const_cast<ChineseChessBoard*>(this);
  const uint8_t fromV = cells[m.from];
  const uint8_t toV = cells[m.to];
  self->cells[m.to] = fromV;
  self->cells[m.from] = Empty;
  const Side opp = (mover == Side::Red) ? Side::Black : Side::Red;
  const bool inChk = inCheck(opp);
  self->cells[m.from] = fromV;
  self->cells[m.to] = toV;
  return inChk;
}

void ChineseChessBoard::recomputeZobrist() {
  uint64_t h = 0;
  for (uint8_t i = 0; i < CELLS; ++i) {
    const uint8_t v = cells[i];
    if (v != Empty) h ^= kZobrist.piece[v][i];
  }
  // Red moves first, so an odd number of plies played means Black to move.
  if (moveCount & 1) h ^= kZobrist.blackToMove;
  zobristHash = h;
}

void ChineseChessBoard::updateResult() {
  // Whose turn it is now (after the move was made).
  const Side mover = nextTurn();
  Move legal[MAX_LEGAL_MOVES];
  const uint8_t n = generateLegalMoves(mover, legal, MAX_LEGAL_MOVES);
  if (n == 0) {
    // No legal moves: it's either checkmate or stalemate. In Xiangqi both
    // count as a loss for the side to move.
    result = (mover == Side::Red) ? Result::BlackWins : Result::RedWins;
  }
}

bool ChineseChessBoard::writeTo(HalFile& f) const {
  const uint8_t version = BOARD_VERSION;
  if (f.write(&version, 1) != 1) return false;
  const uint8_t resultByte = static_cast<uint8_t>(result);
  if (f.write(&resultByte, 1) != 1) return false;
  const uint8_t resignedByte = resigned ? 1 : 0;
  if (f.write(&resignedByte, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&moveCount), sizeof(moveCount)) != sizeof(moveCount)) return false;
  for (uint16_t i = 0; i < moveCount; i++) {
    const Move& m = moveHistory[i];
    if (f.write(&m.from, 1) != 1) return false;
    if (f.write(&m.to, 1) != 1) return false;
    if (f.write(&m.captured, 1) != 1) return false;
  }
  return true;
}

bool ChineseChessBoard::readFrom(HalFile& f) {
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != BOARD_VERSION) {
    LOG_ERR("XQI", "Save board version mismatch (got %u)", static_cast<unsigned>(version));
    return false;
  }
  uint8_t resultByte = 0;
  if (f.read(&resultByte, 1) != 1) return false;
  uint8_t resignedByte = 0;
  if (f.read(&resignedByte, 1) != 1) return false;
  uint16_t mc = 0;
  if (f.read(reinterpret_cast<uint8_t*>(&mc), sizeof(mc)) != sizeof(mc)) return false;
  if (mc > MAX_MOVES) {
    LOG_ERR("XQI", "Save move count out of range: %u", static_cast<unsigned>(mc));
    return false;
  }

  reset();
  for (uint16_t i = 0; i < mc; i++) {
    Move m;
    if (f.read(&m.from, 1) != 1) return false;
    if (f.read(&m.to, 1) != 1) return false;
    if (f.read(&m.captured, 1) != 1) return false;
    if (m.from >= CELLS || m.to >= CELLS) {
      LOG_ERR("XQI", "Save move index out of range");
      return false;
    }
    // Replay the move on the reconstructed board.
    cells[m.to] = cells[m.from];
    cells[m.from] = Empty;
    moveHistory[moveCount++] = m;
  }
  result = static_cast<Result>(resultByte);
  resigned = (resignedByte != 0);
  // moveCount is now authoritative; rebuild the hash from cells + side parity.
  recomputeZobrist();
  return true;
}
