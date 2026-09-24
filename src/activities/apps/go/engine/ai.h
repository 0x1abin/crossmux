#pragma once

// ==================== ship NN configuration ====================
// Decision #3 (2026-08-05): opening net ON, core-18f tier (+DAgger
// training data), quiet gate 2. Measured: tier 29.5% vs full 33.5%
// on fresh paired games; buys ~2K flash toward the device target.
// Host experiment builds: -D duplicates are value-1 safe; pass
// -DNN_FULL_MODEL to build the untrimmed net instead.
#ifdef NO_NNOPEN
#undef NNOPEN            /* beats the build system's -DNNOPEN too */
#else
#ifndef NNOPEN
#define NNOPEN 1
#endif
#endif
/* Learned widen prior (F19 int8 MLP), THE prior since 2107ccf —
 * SHIP DEFAULT 2026-08-14: L0 1477/3000 vs anchor 1346, human 774/2000
 * vs anchor 378/1000. The hand-prior engine and its NO_PRIORNN A/B
 * hatch were deleted 08-19 (dead code cleanup); the full hand engine
 * lives in git history pre-2107ccf and in test/ai_base_probe.cpp. */
#define PRIORNN 1
#define PRIORNN_V2 1
#ifndef NN_FULL_MODEL
#ifndef NN_DEVICE_TIER
#define NN_DEVICE_TIER 1
#endif
#ifndef NN_CORE_TIER
#define NN_CORE_TIER 1
#endif
#endif


#include "game.h"

class AI {
    public:
#ifdef PONDER_SIM
    void ponderThink(Game &game, uint16_t iters); // host sim: think on the
                                 // opponent position w/o disturbing resign/vKomi
#endif
    void reset();

    // Call for every stone placed (either player) to advance the book walk
    void notifyMove(uint8_t x, uint8_t y);
    void notifyPass();

    // Opening book. Returns 1 if a move was made, 0 if off book.
    uint8_t chooseMove(Game &game);

    // Blocking MCTS once off book. Borrows the screen buffer as the
    // node pool — draw and display() a "thinking" frame first (the OLED
    // retains it), and redraw after. Then read bestMove (0 = pass).
    // If think() sets `resigned`, the game is hopeless: skip bestMove
    // and end the game as a resignation.
    void think(Game &game);
    uint8_t bestMove(Game &game, uint8_t &x, uint8_t &y);

    // Game-over scoring: playout-vote dead stones off the board
    // (mutates game: removes them as captures), then computeScore.
    void scoreDead(Game &game);
    uint8_t resigned;

    // Filled by bestMove for the info panel: chosen move's win rate
    // (percent) and visits. (The panel's total-sims denominator reads
    // thinkSims directly -- see below.)
    uint8_t statPct;
    uint16_t statVisits;
    // statTotal retired (08-29): thinkSims itself survives untouched
    // from think-end through the whole stats-display window (zeroed only
    // at the NEXT think start, during which the THINKING frame shows) --
    // the latch was redundant. Display reads thinkSims (extern).

    private:
#ifdef NNOPEN
    uint8_t nnLast;      // last move (0xFF none): opening-net feature
    uint8_t nnOpeningMove(Game &game, uint8_t &ox, uint8_t &oy);
#endif
    uint8_t firstMove;   // 1 until any stone has been played
    uint8_t passToWin;   // set by think(): pass now to end a won game
    uint8_t resignCount; // consecutive hopeless searches (strict tier)
    uint8_t resignCount2; // consecutive bad searches (mild tier)

  public:
    // Runtime RAM-layout guard (halts in setup() if violated): 0 = ok,
    // 1 = node pool reaches the 0x800 magic key, 2 = boardAt carry-free
    // bound broken. Re-added after checkmagic.sh silently went stale.
    uint8_t layoutHazard();
};
