#include "GomokuAI.h"

#include <Arduino.h>
#include <Logging.h>

#include <cstring>

namespace {

using Stone = GomokuBoard::Stone;

constexpr uint8_t kEmpty = static_cast<uint8_t>(Stone::Empty);

// Four line directions: horizontal, vertical, diag down-right, diag up-right.
constexpr int8_t kDir[4][2] = {{0, 1}, {1, 0}, {1, 1}, {-1, 1}};

// Run scores: [length][openness], openness 0=both ends blocked, 1=one open, 2=both open.
// Five-in-a-row dwarfs all other patterns; open-four is essentially won
// (opponent can only block one end, leaving a closed-four that becomes five).
// These magnitudes are the same family used in most open-source gomoku engines.
constexpr int32_t kRunScore[6][3] = {
    {0, 0, 0},                       // length 0 (unused)
    {0, 0, 0},                       // length 1 — isolated stone, ignore
    {0, 10, 100},                    // length 2: closed-2 / open-2
    {0, 1000, 10000},                // length 3: closed-3 / open-3 (a.k.a. live three)
    {0, 100000, 1000000},            // length 4: closed-4 / open-4 (open-4 = forced win)
    {10000000, 10000000, 10000000},  // length 5: five-in-a-row = WIN
};

constexpr int32_t kWinScore = 10000000;
// Slight defensive bias so a tied attack/defense leans defensive — humans
// punish unblocked opp threats faster than they exploit equal-value attacks.
constexpr int32_t kDefenseBias = 12;  // /10 → multiplier 1.2

struct LevelConfig {
  uint8_t maxDepth;
  uint8_t neighborhood;  // candidate radius around existing stones
  uint8_t branchTopK;
  uint16_t timeBudgetMs;
  uint8_t jitterPercent;      // 0..100; static-eval noise at root
  uint8_t suboptimalPercent;  // 0..100; chance to drop the best and use 2nd-best
};

constexpr LevelConfig kLevelTable[3] = {
    // Easy: shallow search + heavy noise → catches obvious threats but blunders often.
    {.maxDepth = 2,
     .neighborhood = 1,
     .branchTopK = 8,
     .timeBudgetMs = 600,
     .jitterPercent = 30,
     .suboptimalPercent = 25},
    // Medium: solid threat handling for casual play.
    {.maxDepth = 4,
     .neighborhood = 2,
     .branchTopK = 12,
     .timeBudgetMs = 2000,
     .jitterPercent = 0,
     .suboptimalPercent = 0},
    // Hard: iterative deepening 2→6 inside time budget.
    {.maxDepth = 6,
     .neighborhood = 2,
     .branchTopK = 16,
     .timeBudgetMs = 4000,
     .jitterPercent = 0,
     .suboptimalPercent = 0},
};

class Searcher {
 public:
  Searcher(const GomokuBoard& board, Stone aiSide, GomokuAiLevel level)
      : boardSize(board.boardSize),
        totalCells(static_cast<uint16_t>(board.boardSize) * board.boardSize),
        aiSide(aiSide),
        oppSide(aiSide == Stone::Black ? Stone::White : Stone::Black),
        cfg(kLevelTable[static_cast<uint8_t>(level)]) {
    std::memcpy(cells, board.cells, totalCells);
    rebuildNeighborMap();
  }

  uint8_t run(uint32_t& outNodes, uint8_t& outDepthReached) {
    startMs = millis();
    nodes = 0;
    timedOut = false;

    // 1) Immediate-win check: if any candidate makes 5-in-a-row for AI → take it.
    uint8_t winNow = findImmediateWin(static_cast<uint8_t>(aiSide));
    if (winNow != GomokuBoard::INVALID_IDX) {
      outNodes = nodes;
      outDepthReached = 0;
      return winNow;
    }
    // 2) Immediate-block: if opp has a winning move next turn → block it.
    uint8_t mustBlock = findImmediateWin(static_cast<uint8_t>(oppSide));
    if (mustBlock != GomokuBoard::INVALID_IDX) {
      outNodes = nodes;
      outDepthReached = 0;
      return mustBlock;
    }

    // 3) Iterative deepening within time budget.
    uint8_t bestMove = GomokuBoard::INVALID_IDX;
    int32_t bestScore = INT32_MIN;
    const uint8_t maxD = cfg.maxDepth;

    uint8_t cands[kMaxCands];
    int32_t scores[kMaxCands];
    int candCount = generateCandidates(cands, kMaxCands);
    if (candCount == 0) {
      // No neighbours — board likely empty. Fall back to centre (caller should
      // not actually reach this since player moves first, but keep safe).
      const uint8_t center = static_cast<uint8_t>((boardSize / 2) * boardSize + (boardSize / 2));
      outNodes = nodes;
      outDepthReached = 0;
      return cells[center] == kEmpty ? center : 0;
    }

    rankCandidates(cands, scores, candCount, static_cast<uint8_t>(aiSide));
    if (candCount > cfg.branchTopK) candCount = cfg.branchTopK;

    // For Easy, depth==2 is fine without iterative deepening overhead.
    // For Hard we deepen 2,4,6 (even depths only — keeps min/max symmetric).
    uint8_t depthStart = (maxD <= 2) ? maxD : 2;
    uint8_t depthStep = 2;
    uint8_t depthReached = 0;

    for (uint8_t d = depthStart; d <= maxD; d += depthStep) {
      int32_t curBest = INT32_MIN;
      uint8_t curBestMove = cands[0];
      int32_t alpha = -kWinScore - 1;
      const int32_t beta = kWinScore + 1;
      for (int i = 0; i < candCount; i++) {
        if (timeUp()) break;
        const uint8_t mv = cands[i];
        placeAt(mv, static_cast<uint8_t>(aiSide));
        const int32_t s = -alphaBeta(d - 1, -beta, -alpha, oppSide);
        undoAt(mv);
        if (timedOut) break;
        if (s > curBest) {
          curBest = s;
          curBestMove = mv;
        }
        if (s > alpha) alpha = s;  // tighten window for remaining root moves
      }
      if (timedOut) {
        // Partial result for this depth is unreliable — keep prior depth's result.
        break;
      }
      bestScore = curBest;
      bestMove = curBestMove;
      depthReached = d;
      // If we already found a guaranteed win at this depth, no need to deepen.
      if (curBest >= kWinScore - 1000) break;
    }

    // Easy-level humanisation: random jitter + occasional 2nd-best pick.
    if (cfg.jitterPercent > 0 || cfg.suboptimalPercent > 0) {
      // Re-rank top candidates by single-step eval and apply noise.
      rankCandidates(cands, scores, candCount, static_cast<uint8_t>(aiSide));
      const int32_t spread = (cfg.jitterPercent * (scores[0] >= 0 ? scores[0] : -scores[0])) / 100;
      for (int i = 0; i < candCount; i++) {
        if (spread > 0) scores[i] += static_cast<int32_t>(random(-spread, spread + 1));
      }
      int bestI = 0;
      for (int i = 1; i < candCount; i++) {
        if (scores[i] > scores[bestI]) bestI = i;
      }
      // Don't override a search-discovered forced win or must-block.
      const bool searchFoundCritical = (bestScore >= kWinScore / 2);
      if (!searchFoundCritical) {
        bestMove = cands[bestI];
        if (cfg.suboptimalPercent > 0 && candCount >= 2 && random(100) < cfg.suboptimalPercent) {
          // Find 2nd-best and use it.
          int secI = (bestI == 0) ? 1 : 0;
          for (int i = 0; i < candCount; i++) {
            if (i == bestI) continue;
            if (scores[i] > scores[secI]) secI = i;
          }
          bestMove = cands[secI];
        }
      }
    }

    if (bestMove == GomokuBoard::INVALID_IDX) bestMove = cands[0];
    outNodes = nodes;
    outDepthReached = depthReached ? depthReached : depthStart;
    return bestMove;
  }

 private:
  static constexpr int kMaxCands = 48;
  static constexpr int kYieldEveryNodes = 64;

  uint8_t cells[GomokuBoard::MAX_CELLS];
  uint8_t neighborCount[GomokuBoard::MAX_CELLS];  // # of stones within radius `cfg.neighborhood`
  uint8_t boardSize;
  uint16_t totalCells;
  Stone aiSide;
  Stone oppSide;
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

  void rebuildNeighborMap() {
    std::memset(neighborCount, 0, totalCells);
    const int n = boardSize;
    const int rad = cfg.neighborhood;
    for (int r = 0; r < n; r++) {
      for (int c = 0; c < n; c++) {
        if (cells[r * n + c] == kEmpty) continue;
        for (int dr = -rad; dr <= rad; dr++) {
          for (int dc = -rad; dc <= rad; dc++) {
            const int rr = r + dr;
            const int cc = c + dc;
            if (rr < 0 || rr >= n || cc < 0 || cc >= n) continue;
            neighborCount[rr * n + cc]++;
          }
        }
      }
    }
  }

  void placeAt(uint8_t idx, uint8_t color) {
    cells[idx] = color;
    const int n = boardSize;
    const int r = idx / n;
    const int c = idx % n;
    const int rad = cfg.neighborhood;
    for (int dr = -rad; dr <= rad; dr++) {
      for (int dc = -rad; dc <= rad; dc++) {
        const int rr = r + dr;
        const int cc = c + dc;
        if (rr < 0 || rr >= n || cc < 0 || cc >= n) continue;
        neighborCount[rr * n + cc]++;
      }
    }
  }

  void undoAt(uint8_t idx) {
    cells[idx] = kEmpty;
    const int n = boardSize;
    const int r = idx / n;
    const int c = idx % n;
    const int rad = cfg.neighborhood;
    for (int dr = -rad; dr <= rad; dr++) {
      for (int dc = -rad; dc <= rad; dc++) {
        const int rr = r + dr;
        const int cc = c + dc;
        if (rr < 0 || rr >= n || cc < 0 || cc >= n) continue;
        if (neighborCount[rr * n + cc] > 0) neighborCount[rr * n + cc]--;
      }
    }
  }

  // Returns first cell where placing `color` makes 5-in-a-row, or INVALID_IDX.
  uint8_t findImmediateWin(uint8_t color) {
    const int n = boardSize;
    for (uint16_t i = 0; i < totalCells; i++) {
      if (cells[i] != kEmpty) continue;
      if (neighborCount[i] == 0) continue;
      const int r = i / n;
      const int c = i % n;
      // Check all 4 directions: would placing here create len >= 5?
      for (int d = 0; d < 4; d++) {
        const int dr = kDir[d][0];
        const int dc = kDir[d][1];
        int len = 1;
        for (int k = 1; k < 5; k++) {
          const int rr = r + dr * k;
          const int cc = c + dc * k;
          if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
          if (cells[rr * n + cc] != color) break;
          len++;
        }
        for (int k = 1; k < 5; k++) {
          const int rr = r - dr * k;
          const int cc = c - dc * k;
          if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
          if (cells[rr * n + cc] != color) break;
          len++;
        }
        if (len >= 5) return static_cast<uint8_t>(i);
      }
    }
    return GomokuBoard::INVALID_IDX;
  }

  int generateCandidates(uint8_t out[], int cap) {
    int count = 0;
    for (uint16_t i = 0; i < totalCells && count < cap; i++) {
      if (cells[i] == kEmpty && neighborCount[i] > 0) {
        out[count++] = static_cast<uint8_t>(i);
      }
    }
    return count;
  }

  // Score a single hypothetical placement: contribution to attack + defense.
  int32_t scorePoint(uint8_t idx, uint8_t color) const {
    const int n = boardSize;
    const int r = idx / n;
    const int c = idx % n;
    int32_t total = 0;
    for (int d = 0; d < 4; d++) {
      const int dr = kDir[d][0];
      const int dc = kDir[d][1];
      // Count consecutive `color` extending forward from (r,c).
      int fwd = 0;
      bool fwdOpen = false;
      for (int k = 1; k < 5; k++) {
        const int rr = r + dr * k;
        const int cc = c + dc * k;
        if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
        const uint8_t v = cells[rr * n + cc];
        if (v == color) {
          fwd++;
          continue;
        }
        fwdOpen = (v == kEmpty);
        break;
      }
      int bwd = 0;
      bool bwdOpen = false;
      for (int k = 1; k < 5; k++) {
        const int rr = r - dr * k;
        const int cc = c - dc * k;
        if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
        const uint8_t v = cells[rr * n + cc];
        if (v == color) {
          bwd++;
          continue;
        }
        bwdOpen = (v == kEmpty);
        break;
      }
      const int len = fwd + bwd + 1;
      const int openness = (fwdOpen ? 1 : 0) + (bwdOpen ? 1 : 0);
      const int clampedLen = (len > 5) ? 5 : len;
      total += kRunScore[clampedLen][openness];
    }
    return total;
  }

  // Static board evaluation from `aiSide` perspective. Positive = AI good.
  // Iterates over starting positions of own runs (skip cells whose previous
  // cell in the direction is the same colour) so each run is counted once.
  int32_t evaluate() {
    const int n = boardSize;
    int32_t myScore = 0;
    int32_t oppScore = 0;
    const uint8_t my = static_cast<uint8_t>(aiSide);
    const uint8_t op = static_cast<uint8_t>(oppSide);
    for (int d = 0; d < 4; d++) {
      const int dr = kDir[d][0];
      const int dc = kDir[d][1];
      for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
          const uint8_t v = cells[r * n + c];
          if (v == kEmpty) continue;
          // Only count from runs whose first stone is here.
          const int pr = r - dr;
          const int pc = c - dc;
          if (pr >= 0 && pr < n && pc >= 0 && pc < n && cells[pr * n + pc] == v) continue;

          int len = 1;
          int rr = r + dr;
          int cc = c + dc;
          while (rr >= 0 && rr < n && cc >= 0 && cc < n && cells[rr * n + cc] == v) {
            len++;
            rr += dr;
            cc += dc;
          }
          // Determine open/closed at both ends.
          // Back-end: (pr,pc); Forward-end: (rr,cc).
          const bool backOpen = (pr >= 0 && pr < n && pc >= 0 && pc < n) && cells[pr * n + pc] == kEmpty;
          const bool fwdOpen = (rr >= 0 && rr < n && cc >= 0 && cc < n) && cells[rr * n + cc] == kEmpty;
          const int openness = (backOpen ? 1 : 0) + (fwdOpen ? 1 : 0);
          const int clampedLen = (len > 5) ? 5 : len;
          const int32_t s = kRunScore[clampedLen][openness];
          if (v == my) {
            myScore += s;
          } else if (v == op) {
            oppScore += s;
          }
        }
      }
    }
    return myScore - (oppScore * kDefenseBias) / 10;
  }

  void rankCandidates(uint8_t cands[], int32_t scores[], int count, uint8_t aiColor) {
    const uint8_t opp = (aiColor == static_cast<uint8_t>(Stone::Black)) ? static_cast<uint8_t>(Stone::White)
                                                                        : static_cast<uint8_t>(Stone::Black);
    for (int i = 0; i < count; i++) {
      const int32_t off = scorePoint(cands[i], aiColor);
      const int32_t def = scorePoint(cands[i], opp);
      scores[i] = off + def;  // dual-purpose: extending own threats + denying opp
    }
    // Insertion-sort descending — simple and good enough for ≤48 entries.
    for (int i = 1; i < count; i++) {
      const uint8_t mv = cands[i];
      const int32_t sc = scores[i];
      int j = i;
      while (j > 0 && scores[j - 1] < sc) {
        cands[j] = cands[j - 1];
        scores[j] = scores[j - 1];
        j--;
      }
      cands[j] = mv;
      scores[j] = sc;
    }
  }

  // Negamax: returns best score from `sideToMove`'s perspective.
  // Caller flips sign so AI sees its own perspective at root.
  int32_t alphaBeta(int depth, int32_t alpha, int32_t beta, Stone sideToMove) {
    nodes++;
    if ((nodes & (kYieldEveryNodes - 1)) == 0) {
      yield();
      if (timeUp()) return 0;
    }

    // Win detection: previous move (parent's placement) might have made 5;
    // check is implicit in evaluation (FIVE pattern dominates).
    if (depth == 0) {
      const int32_t e = evaluate();
      // If sideToMove is opp, evaluate returns AI's perspective; negate so
      // negamax convention holds (caller will negate it back).
      return (sideToMove == aiSide) ? e : -e;
    }

    uint8_t cands[kMaxCands];
    int32_t scores[kMaxCands];
    int count = generateCandidates(cands, kMaxCands);
    if (count == 0) return 0;
    rankCandidates(cands, scores, count, static_cast<uint8_t>(sideToMove));
    if (count > cfg.branchTopK) count = cfg.branchTopK;

    int32_t best = INT32_MIN + 1;
    const Stone next = (sideToMove == Stone::Black) ? Stone::White : Stone::Black;
    for (int i = 0; i < count; i++) {
      if (timeUp()) break;
      const uint8_t mv = cands[i];
      // Quick win short-circuit: if this move makes 5, return win immediately.
      placeAt(mv, static_cast<uint8_t>(sideToMove));
      // Detect 5-in-a-row at (mv) without full board eval.
      const int n = boardSize;
      const int r = mv / n;
      const int c = mv % n;
      bool madeFive = false;
      for (int d = 0; d < 4 && !madeFive; d++) {
        const int dr = kDir[d][0];
        const int dc = kDir[d][1];
        int len = 1;
        for (int k = 1; k < 5; k++) {
          const int rr = r + dr * k;
          const int cc = c + dc * k;
          if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
          if (cells[rr * n + cc] != static_cast<uint8_t>(sideToMove)) break;
          len++;
        }
        for (int k = 1; k < 5; k++) {
          const int rr = r - dr * k;
          const int cc = c - dc * k;
          if (rr < 0 || rr >= n || cc < 0 || cc >= n) break;
          if (cells[rr * n + cc] != static_cast<uint8_t>(sideToMove)) break;
          len++;
        }
        if (len >= 5) madeFive = true;
      }
      int32_t s;
      if (madeFive) {
        s = kWinScore - (cfg.maxDepth - depth);  // prefer faster wins
      } else {
        s = -alphaBeta(depth - 1, -beta, -alpha, next);
      }
      undoAt(mv);
      if (timedOut) break;
      if (s > best) best = s;
      if (best > alpha) alpha = best;
      if (alpha >= beta) break;  // beta cutoff
    }
    return best;
  }
};

}  // namespace

uint8_t GomokuAI::chooseMove(const GomokuBoard& board, GomokuBoard::Stone aiSide, GomokuAiLevel level) {
  Searcher s(board, aiSide, level);
  uint32_t nodes = 0;
  uint8_t depthReached = 0;
  const uint32_t t0 = millis();
  const uint8_t move = s.run(nodes, depthReached);
  const uint32_t dt = millis() - t0;
  LOG_INF("GMK_AI", "level=%u depth=%u nodes=%lu ms=%lu move=%u", static_cast<unsigned>(level),
          static_cast<unsigned>(depthReached), static_cast<unsigned long>(nodes), static_cast<unsigned long>(dt),
          static_cast<unsigned>(move));
  return move;
}
