#include "ChineseChessAI.h"

#include <Arduino.h>
#include <Logging.h>

#include <cstdint>
#include <cstring>

namespace {

using Side = ChineseChessBoard::Side;
using Kind = ChineseChessBoard::Kind;
using Move = ChineseChessBoard::Move;

// Piece values in centipawns. Index by Kind.
constexpr int32_t kPieceValue[7] = {
    10000,  // King (treat as game-over weight)
    200,    // Advisor
    200,    // Elephant
    400,    // Horse
    900,    // Chariot
    450,    // Cannon
    100,    // Pawn (base)
};

constexpr int32_t kCrossedPawnBonus = 100;  // pawn that crossed the river

constexpr int32_t kKingCaptured = 100000;
constexpr int32_t kInfScore = 1000000;

struct LevelConfig {
  uint8_t maxDepth;
  uint16_t timeBudgetMs;
  uint8_t jitterPercent;
  uint8_t suboptimalPercent;
};

constexpr LevelConfig kLevelTable[3] = {
    {.maxDepth = 2, .timeBudgetMs = 800, .jitterPercent = 25, .suboptimalPercent = 25},  // Easy
    {.maxDepth = 4, .timeBudgetMs = 2500, .jitterPercent = 0, .suboptimalPercent = 0},   // Medium
    {.maxDepth = 6, .timeBudgetMs = 5000, .jitterPercent = 0, .suboptimalPercent = 0},   // Hard
};

class Searcher {
 public:
  Searcher(const ChineseChessBoard& src, Side aiSide, ChineseChessAiLevel level)
      : board(src),
        aiSide(aiSide),
        oppSide(aiSide == Side::Red ? Side::Black : Side::Red),
        cfg(kLevelTable[static_cast<uint8_t>(level)]) {}

  Move run(uint32_t& outNodes, uint8_t& outDepthReached) {
    startMs = millis();
    nodes = 0;
    timedOut = false;

    Move rootMoves[ChineseChessBoard::MAX_LEGAL_MOVES];
    uint8_t rootCount = board.generateLegalMoves(aiSide, rootMoves, ChineseChessBoard::MAX_LEGAL_MOVES);
    if (rootCount == 0) {
      outNodes = 0;
      outDepthReached = 0;
      return Move{};  // signal: no legal move
    }

    uint8_t nonRepeatingCount = 0;
    for (uint8_t i = 0; i < rootCount; i++) {
      if (!repeatsPriorPosition(rootMoves[i])) rootMoves[nonRepeatingCount++] = rootMoves[i];
    }
    if (nonRepeatingCount > 0) rootCount = nonRepeatingCount;

    orderMoves(rootMoves, rootCount, /*ordering*/ true);

    Move bestMove = rootMoves[0];
    int32_t bestScore = -kInfScore;
    uint8_t depthReached = 0;

    // Iterative deepening 2,4,...,maxDepth (even depths only — keeps min/max symmetric).
    const uint8_t maxD = cfg.maxDepth;
    uint8_t depthStart = (maxD <= 2) ? maxD : 2;
    uint8_t depthStep = 2;
    int32_t scoreBuf[ChineseChessBoard::MAX_LEGAL_MOVES] = {};

    for (uint8_t d = depthStart; d <= maxD; d += depthStep) {
      int32_t curBest = -kInfScore;
      Move curBestMove = rootMoves[0];
      int32_t alpha = -kInfScore;
      const int32_t beta = kInfScore;
      bool partial = false;
      for (uint8_t i = 0; i < rootCount; i++) {
        if (timeUp()) {
          partial = true;
          break;
        }
        const Move m = rootMoves[i];
        board.makeMove(m);
        const int32_t s = -alphaBeta(d - 1, -beta, -alpha, oppSide);
        board.undo();
        if (timedOut) {
          partial = true;
          break;
        }
        scoreBuf[i] = s;
        if (s > curBest) {
          curBest = s;
          curBestMove = m;
        }
        if (s > alpha) alpha = s;
      }
      if (partial) {
        // Discard partial-depth result (can be misleading); keep prior depth.
        break;
      }
      bestScore = curBest;
      bestMove = curBestMove;
      depthReached = d;
      // Reorder root moves by latest scores so next iteration searches likely-good first.
      reorderByScore(rootMoves, scoreBuf, rootCount);
      if (curBest >= kKingCaptured / 2) break;  // forced win discovered
    }

    // Easy-level humanisation.
    if (cfg.jitterPercent > 0 || cfg.suboptimalPercent > 0) {
      int32_t baseScores[ChineseChessBoard::MAX_LEGAL_MOVES] = {};
      for (uint8_t i = 0; i < rootCount; i++) {
        baseScores[i] = staticMoveScore(rootMoves[i]);
      }
      const int32_t spread = (cfg.jitterPercent * (baseScores[0] >= 0 ? baseScores[0] : -baseScores[0])) / 100;
      for (uint8_t i = 0; i < rootCount; i++) {
        if (spread > 0) baseScores[i] += static_cast<int32_t>(random(-spread, spread + 1));
      }
      uint8_t bestI = 0;
      for (uint8_t i = 1; i < rootCount; i++) {
        if (baseScores[i] > baseScores[bestI]) bestI = i;
      }
      const bool searchFoundCritical = (bestScore >= kKingCaptured / 2);
      if (!searchFoundCritical) {
        bestMove = rootMoves[bestI];
        if (cfg.suboptimalPercent > 0 && rootCount >= 2 && random(100) < cfg.suboptimalPercent) {
          uint8_t secI = (bestI == 0) ? 1 : 0;
          for (uint8_t i = 0; i < rootCount; i++) {
            if (i == bestI) continue;
            if (baseScores[i] > baseScores[secI]) secI = i;
          }
          bestMove = rootMoves[secI];
        }
      }
    }

    outNodes = nodes;
    outDepthReached = depthReached ? depthReached : depthStart;
    return bestMove;
  }

 private:
  static constexpr int kYieldEveryNodes = 64;

  ChineseChessBoard board;
  Side aiSide;
  Side oppSide;
  LevelConfig cfg;

  uint32_t startMs = 0;
  uint32_t nodes = 0;
  bool timedOut = false;

  bool timeUp() {
    if (timedOut) return true;
    if ((millis() - startMs) >= cfg.timeBudgetMs) {
      timedOut = true;
      return true;
    }
    return false;
  }

  bool repeatsPriorPosition(const Move& candidate) {
    board.makeMove(candidate);
    uint8_t candidateCells[ChineseChessBoard::CELLS];
    std::memcpy(candidateCells, board.cells, sizeof(candidateCells));
    const uint16_t candidateMoveCount = board.moveCount;
    bool repeated = false;

    while (board.moveCount >= 2) {
      const bool crossedCapture = board.moveHistory[board.moveCount - 1].captured != ChineseChessBoard::Empty ||
                                  board.moveHistory[board.moveCount - 2].captured != ChineseChessBoard::Empty;
      board.undo();
      board.undo();
      repeated = std::memcmp(candidateCells, board.cells, sizeof(candidateCells)) == 0;
      if (repeated || crossedCapture) break;
    }

    while (board.moveCount < candidateMoveCount) {
      const Move replay = board.moveHistory[board.moveCount];
      board.makeMove(replay);
    }
    board.undo();
    return repeated;
  }

  // MVV-LVA: rank captures by victim value descending, then attacker value ascending.
  int32_t staticMoveScore(const Move& m) const {
    const uint8_t mover = board.cells[m.from];
    const uint8_t target = board.cells[m.to];
    int32_t s = 0;
    if (target != ChineseChessBoard::Empty) {
      s += kPieceValue[static_cast<uint8_t>(ChineseChessBoard::kindOf(target))] * 16;
      if (mover != ChineseChessBoard::Empty) {
        s -= kPieceValue[static_cast<uint8_t>(ChineseChessBoard::kindOf(mover))];
      }
    }
    return s;
  }

  void orderMoves(Move* moves, uint8_t n, bool /*forCaptures*/) const {
    int32_t scores[ChineseChessBoard::MAX_LEGAL_MOVES];
    for (uint8_t i = 0; i < n; i++) scores[i] = staticMoveScore(moves[i]);
    // Insertion sort descending.
    for (uint8_t i = 1; i < n; i++) {
      const Move m = moves[i];
      const int32_t sc = scores[i];
      int j = i;
      while (j > 0 && scores[j - 1] < sc) {
        moves[j] = moves[j - 1];
        scores[j] = scores[j - 1];
        j--;
      }
      moves[j] = m;
      scores[j] = sc;
    }
  }

  void reorderByScore(Move* moves, int32_t* scores, uint8_t n) const {
    for (uint8_t i = 1; i < n; i++) {
      const Move m = moves[i];
      const int32_t sc = scores[i];
      int j = i;
      while (j > 0 && scores[j - 1] < sc) {
        moves[j] = moves[j - 1];
        scores[j] = scores[j - 1];
        j--;
      }
      moves[j] = m;
      scores[j] = sc;
    }
  }

  // Static board evaluation from `aiSide` perspective. Positive = AI good.
  int32_t evaluate() const {
    int32_t my = 0;
    int32_t opp = 0;
    bool myKing = false, oppKing = false;
    for (uint8_t i = 0; i < ChineseChessBoard::CELLS; i++) {
      const uint8_t v = board.cells[i];
      if (v == ChineseChessBoard::Empty) continue;
      const Kind k = ChineseChessBoard::kindOf(v);
      const Side s = ChineseChessBoard::sideOf(v);
      int32_t value = kPieceValue[static_cast<uint8_t>(k)];
      if (k == Kind::Pawn) {
        const uint8_t r = ChineseChessBoard::rowOf(i);
        const bool crossed = (s == Side::Red) ? (r <= 4) : (r >= 5);
        if (crossed) value += kCrossedPawnBonus;
      }
      if (k == Kind::King) {
        if (s == aiSide)
          myKing = true;
        else
          oppKing = true;
      }
      if (s == aiSide)
        my += value;
      else
        opp += value;
    }
    if (!myKing) return -kKingCaptured;
    if (!oppKing) return kKingCaptured;
    return my - opp;
  }

  int32_t alphaBeta(int depth, int32_t alpha, int32_t beta, Side stm) {
    nodes++;
    if ((nodes & (kYieldEveryNodes - 1)) == 0) {
      yield();
      if (timeUp()) return 0;
    }

    if (depth == 0) {
      const int32_t e = evaluate();
      return (stm == aiSide) ? e : -e;
    }

    Move moves[ChineseChessBoard::MAX_LEGAL_MOVES];
    const uint8_t n = board.generateLegalMoves(stm, moves, ChineseChessBoard::MAX_LEGAL_MOVES);
    if (n == 0) {
      // Side to move has no legal moves: lost.
      return -kKingCaptured + (cfg.maxDepth - depth);
    }
    orderMoves(moves, n, true);

    const Side next = (stm == Side::Red) ? Side::Black : Side::Red;
    int32_t best = -kInfScore;
    for (uint8_t i = 0; i < n; i++) {
      if (timeUp()) break;
      const Move m = moves[i];
      // King-capture short-circuit: this move grabs the opponent's king.
      const uint8_t target = board.cells[m.to];
      if (target != ChineseChessBoard::Empty && ChineseChessBoard::kindOf(target) == Kind::King) {
        return kKingCaptured - (cfg.maxDepth - depth);  // prefer faster wins
      }
      board.makeMove(m);
      const int32_t s = -alphaBeta(depth - 1, -beta, -alpha, next);
      board.undo();
      if (timedOut) break;
      if (s > best) best = s;
      if (best > alpha) alpha = best;
      if (alpha >= beta) break;  // beta cutoff
    }
    return best;
  }
};

}  // namespace

ChineseChessBoard::Move ChineseChessAI::chooseMove(const ChineseChessBoard& board, ChineseChessBoard::Side aiSide,
                                                   ChineseChessAiLevel level) {
  Searcher s(board, aiSide, level);
  uint32_t nodes = 0;
  uint8_t depthReached = 0;
  const uint32_t t0 = millis();
  const Move move = s.run(nodes, depthReached);
  const uint32_t dt = millis() - t0;
  LOG_INF("XQI_AI", "level=%u depth=%u nodes=%lu ms=%lu from=%u to=%u", static_cast<unsigned>(level),
          static_cast<unsigned>(depthReached), static_cast<unsigned long>(nodes), static_cast<unsigned long>(dt),
          static_cast<unsigned>(move.from), static_cast<unsigned>(move.to));
  return move;
}
