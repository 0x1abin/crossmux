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

// Repetition / perpetual-check scoring.
// kRepetitionLossBase is intentionally less than kKingCaptured so a real mate
// beats a "you'd lose to 长将" verdict when both are findable.
constexpr int32_t kRepetitionLossBase = kKingCaptured - 1000;
constexpr int32_t kContempt = 30;  // centipawns; sign depends on material balance

// Path-tracking flag bits stored per ply in Searcher::pathFlags[].
constexpr uint8_t F_CHECK = 1 << 0;  // the move that produced this position delivered check
constexpr uint8_t F_RED = 1 << 1;    // the mover that produced this position was Red
constexpr uint8_t F_CAPT = 1 << 2;   // the move that produced this position was a capture

struct LevelConfig {
  uint8_t maxDepth;
  uint16_t timeBudgetMs;
  uint8_t jitterPercent;
  uint8_t suboptimalPercent;
};

constexpr LevelConfig kLevelTable[3] = {
    // Easy depth 2 cannot structurally observe a 2-ply perpetual-check cycle
    // (the cycle re-enters its start at depth 4). Easy relies on jitter +
    // suboptimal-move sampling to avoid degenerate loops in practice.
    {.maxDepth = 2, .timeBudgetMs = 800, .jitterPercent = 25, .suboptimalPercent = 25},  // Easy
    {.maxDepth = 4, .timeBudgetMs = 2500, .jitterPercent = 0, .suboptimalPercent = 0},   // Medium
    {.maxDepth = 6, .timeBudgetMs = 5000, .jitterPercent = 0, .suboptimalPercent = 0},   // Hard
};

class Searcher {
 public:
  Searcher(const ChineseChessBoard& src, Side aiSide, ChineseChessAiLevel level)
      : aiSide(aiSide),
        oppSide(aiSide == Side::Red ? Side::Black : Side::Red),
        cfg(kLevelTable[static_cast<uint8_t>(level)]) {
    board = src;  // copy
    initPath();
  }

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
        const uint8_t preTarget = board.cells[m.to];
        const uint16_t savedIrr = pushSearchMove(aiSide, m, preTarget);
        const int32_t s = -alphaBeta(d - 1, -beta, -alpha, oppSide);
        popSearchMove(savedIrr);
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
      int32_t baseScores[ChineseChessBoard::MAX_LEGAL_MOVES];
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
  // Path stack capacity: full game history (up to MAX_MOVES) plus headroom for
  // the deepest search frame. MAX_MOVES=256, max search depth=6 → 272 entries.
  static constexpr uint16_t kPathCap = ChineseChessBoard::MAX_MOVES + 16;

  ChineseChessBoard board;
  Side aiSide;
  Side oppSide;
  LevelConfig cfg;

  uint32_t startMs = 0;
  uint32_t nodes = 0;
  bool timedOut = false;

  // Per-ply history for repetition / perpetual-check detection. pathHashes[i]
  // is the Zobrist hash of the position BEFORE the i-th half-move was made
  // (so pathHashes[0] is the initial position). pathFlags[i+1] describes the
  // half-move that produced pathHashes[i+1] (the position reached after it).
  // Captures are irreversible in Xiangqi (no promotion back) — when one occurs
  // we advance `lastIrreversibleIndex` so the repetition scan can prune at it.
  uint64_t pathHashes[kPathCap] = {};
  uint8_t pathFlags[kPathCap] = {};
  uint16_t basePly = 0;
  uint16_t currentPly = 0;
  uint16_t lastIrreversibleIndex = 0;
  // Computed once from the static material balance; drives contempt direction.
  bool aiMaterialAhead = false;

  void initPath() {
    // Save the source history, then rebuild board from scratch via makeMove so
    // the new incremental Zobrist machinery populates board.zobristHash at
    // each step. Also records check/capture flags per historical ply.
    ChineseChessBoard::Move savedHistory[ChineseChessBoard::MAX_MOVES];
    const uint16_t savedCount = board.moveCount;
    for (uint16_t i = 0; i < savedCount; ++i) savedHistory[i] = board.moveHistory[i];

    const ChineseChessBoard::Result savedResult = board.result;
    const bool savedResigned = board.resigned;

    board.reset();
    pathHashes[0] = board.zobristHash;
    pathFlags[0] = 0;
    lastIrreversibleIndex = 0;

    for (uint16_t i = 0; i < savedCount; ++i) {
      const ChineseChessBoard::Move& m = savedHistory[i];
      // Mover side is determined by ply parity: index 0 = Red.
      const Side mover = (i % 2 == 0) ? Side::Red : Side::Black;
      const bool gaveCheck = board.givesCheck(mover, m);
      const bool wasCapture = (m.captured != ChineseChessBoard::Empty);
      board.makeMove(m);
      const uint16_t idx = i + 1;
      pathHashes[idx] = board.zobristHash;
      uint8_t f = 0;
      if (gaveCheck) f |= F_CHECK;
      if (mover == Side::Red) f |= F_RED;
      if (wasCapture) {
        f |= F_CAPT;
        lastIrreversibleIndex = idx;
      }
      pathFlags[idx] = f;
    }

    board.result = savedResult;
    board.resigned = savedResigned;
    basePly = savedCount;
    currentPly = basePly;

    // Material balance from AI POV — cheap one-pass scan.
    int32_t myMat = 0, oppMat = 0;
    for (uint8_t i = 0; i < ChineseChessBoard::CELLS; ++i) {
      const uint8_t v = board.cells[i];
      if (v == ChineseChessBoard::Empty) continue;
      const Kind k = ChineseChessBoard::kindOf(v);
      if (k == Kind::King) continue;  // king value swamps everything; skip
      const int32_t pv = kPieceValue[static_cast<uint8_t>(k)];
      if (ChineseChessBoard::sideOf(v) == aiSide)
        myMat += pv;
      else
        oppMat += pv;
    }
    aiMaterialAhead = (myMat >= oppMat);
  }

  // Path stack push/pop, mirrored on both the inner alphaBeta loop and the
  // root loop in run(). Call BEFORE makeMove / AFTER undo respectively. The
  // caller supplies the pre-move target byte so we can detect captures
  // without re-reading the board after the mutation.
  inline uint16_t pushSearchMove(Side mover, const ChineseChessBoard::Move& m, uint8_t preTarget) {
    const bool gaveCheck = board.givesCheck(mover, m);
    const bool wasCapture = (preTarget != ChineseChessBoard::Empty);
    const uint16_t savedIrr = lastIrreversibleIndex;
    board.makeMove(m);
    ++currentPly;
    pathHashes[currentPly] = board.zobristHash;
    uint8_t f = 0;
    if (gaveCheck) f |= F_CHECK;
    if (mover == Side::Red) f |= F_RED;
    if (wasCapture) {
      f |= F_CAPT;
      lastIrreversibleIndex = currentPly;
    }
    pathFlags[currentPly] = f;
    return savedIrr;
  }
  inline void popSearchMove(uint16_t savedIrr) {
    --currentPly;
    lastIrreversibleIndex = savedIrr;
    board.undo();
  }

  // Returns a score from `stm`'s perspective if the current position repeats a
  // prior position within the irreversible window. Returns 0 if no repetition.
  // Caller passes `distanceFromRoot` so the mate-distance accounting matches
  // the rest of the search (faster losses preferred when forced).
  int32_t repetitionScore(Side stm, int distanceFromRoot) const {
    for (uint16_t k = lastIrreversibleIndex; k < currentPly; ++k) {
      if (pathHashes[k] != board.zobristHash) continue;
      // Cycle moves are pathFlags[k+1 .. currentPly] (1-based offset on the
      // flag array because pathFlags[i+1] describes the move that produced
      // position i+1).
      bool aiOnlyChecker = true, oppOnlyChecker = true;
      bool aiHadMove = false, oppHadMove = false;
      const bool aiIsRed = (aiSide == Side::Red);
      for (uint16_t j = k + 1; j <= currentPly; ++j) {
        const bool moverRed = (pathFlags[j] & F_RED) != 0;
        const bool moverIsAi = (moverRed == aiIsRed);
        const bool gaveCheck = (pathFlags[j] & F_CHECK) != 0;
        if (moverIsAi) {
          aiHadMove = true;
          if (!gaveCheck) aiOnlyChecker = false;
        } else {
          oppHadMove = true;
          if (!gaveCheck) oppOnlyChecker = false;
        }
      }
      int32_t aiScore;  // from AI's POV
      const bool aiPerpetual = aiHadMove && aiOnlyChecker && !(oppHadMove && oppOnlyChecker);
      const bool oppPerpetual = oppHadMove && oppOnlyChecker && !(aiHadMove && aiOnlyChecker);
      if (aiPerpetual) {
        // 长将判负: AI is the sole perpetual checker → AI loses.
        aiScore = -(kRepetitionLossBase - distanceFromRoot);
      } else if (oppPerpetual) {
        aiScore = +(kRepetitionLossBase - distanceFromRoot);
      } else {
        // Neutral repetition: lean against drawing when ahead, toward drawing
        // when behind. NOTE: this does not implement 长捉判负 (perpetual chase);
        // tracking threat repetition needs a separate mechanism — out of scope.
        aiScore = aiMaterialAhead ? -kContempt : +kContempt;
      }
      return (stm == aiSide) ? aiScore : -aiScore;
    }
    return 0;
  }

  bool timeUp() {
    if (timedOut) return true;
    if ((millis() - startMs) >= cfg.timeBudgetMs) {
      timedOut = true;
      return true;
    }
    return false;
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

    // Repetition / perpetual-check verdict. Scans only the irreversible
    // window; returns 0 if no prior occurrence is found. Must run BEFORE the
    // depth==0 leaf so a repeated leaf is scored as a cycle, not as material.
    {
      const int32_t rs = repetitionScore(stm, static_cast<int>(currentPly - basePly));
      if (rs != 0) return rs;
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
      // King-capture short-circuit: this move grabs the opponent's king. Skip
      // the path push since this is a terminal node.
      const uint8_t target = board.cells[m.to];
      if (target != ChineseChessBoard::Empty && ChineseChessBoard::kindOf(target) == Kind::King) {
        return kKingCaptured - (cfg.maxDepth - depth);  // prefer faster wins
      }
      // Safety: bail if the path stack would overflow. Should not happen
      // (basePly ≤ 256, max search depth = 6, kPathCap = 272) but a depth
      // bump or a long game without captures could approach it.
      if (currentPly + 1 >= kPathCap) break;
      const uint16_t savedIrr = pushSearchMove(stm, m, target);
      const int32_t s = -alphaBeta(depth - 1, -beta, -alpha, next);
      popSearchMove(savedIrr);
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
