#include "game.h"

void Game::reset() {
    memset(board, EMPTY, sizeof(board));
    lastMove = koCap = 0xFF;
    koExact = 0;
    turn = BLACK;
    captures[0] = captures[1] = 0;
    consecutivePasses = 0;
    cursorX = cursorY = 4;
    kpieces = 13; // 6.5 komi
    area[0] = area[1] = 0;
    resignedBy = 0;
}

uint8_t Game::at(uint8_t x, uint8_t y) {
    return board[y * BOARD_SIZE + x];
}

void Game::set(uint8_t x, uint8_t y, uint8_t val) {
    board[y * BOARD_SIZE + x] = val;
}

uint8_t floodScratch[BOARD_CELLS];

// Shared orthogonal offsets: four functions each materialized these
// as stack locals (init code + 8 stack bytes per instance).
// Shared engine neighbour table (neighbor_table.h, defined in ai.cpp's
// TU): 0xFF-terminated list per cell, stride 5. Replaces the computed
// nbIndexXY/nbIndex pair (mul + bounds checks per call) and the DX4/DY4
// tables (08-29). Walk idiom:
#include "neighbor_table.h"
// simBoard extern + boardAt/CELL_AT come from neighbor_table.h
extern void unpackBoard(Game &game);   // the canonical board->sim sync (ai.cpp)
#define FOR_NB FOR_EACH_NEIGHBOR   // the shared walk (neighbor_table.h)

// The game-side flood family (floodFill/floodClean/ffPush/sweepVisited)
// died 08-29: liberty checks ride the engine's hasLiberty, captures
// sweep its captured-group list, computeScore's territory is fixpoint
// touch-masks. floodScratch stays: the ENGINE's floods stack in it.

// Engine flood, declared here (ai.cpp): pure on simBoard+marks+scratch.
extern uint8_t hasLiberty(uint8_t start, uint8_t color);

uint8_t Game::hasLiberties(uint8_t start) {
    // Sync-then-call (08-29): the board is byte-identical to simBoard's
    // format now, so an 81-byte memcpy buys the engine's paid-for flood
    // (prescan + pointer BFS) and this function's private flood-and-scan
    // body is gone. EMPTY input keeps the old contract. Clobbering
    // simBoard is safe: it is wreckage outside the search, and no game
    // flood holds visited[] across this call.
    uint8_t color = board[start];
    if(color == EMPTY) return 0;
    unpackBoard(*this);
    return hasLiberty(start, color);
}

// Engine gift (Jay 08-29): hasLiberty(start)==0 leaves the WHOLE group
// in floodScratch[0..capturedGroupN-1] -- the same contract simPlay's
// removeGroup rides. playMove calls hasLiberties(ni) immediately before
// this, so the list is live: sweep it into the real board. No second
// flood. INVARIANT: only valid right after hasLiberties(start) == 0.
extern uint8_t capturedGroupN;
uint8_t Game::captureGroup(uint8_t start) {
    (void)start;
    uint8_t n = capturedGroupN;
    for(uint8_t i = 0; i < n; i++) board[floodSlot(i)] = EMPTY;
    return n;
}

uint8_t Game::isValidMove(uint8_t x, uint8_t y) {
    // The rules AUTHORITY: the engine's own move machinery (simPlay) is
    // deliberately approximate for playout speed, so rootMoveOK and the
    // human path both come here for exact legality. Rebuilt 08-29 as a
    // single-sync TRIAL on the engine board: place on simBoard (the
    // real board is never touched -- the old set/unset dance is gone)
    // and run the engine flood directly, one memcpy for the whole
    // validation instead of one per liberty check.
    if(x >= BOARD_SIZE || y >= BOARD_SIZE) return 0;
    uint8_t idx = y * BOARD_SIZE + x;
    if(board[idx] != EMPTY) return 0;

    unpackBoard(*this);
    simBoard[idx] = turn;

    uint8_t opponent = 3 - turn;   // BLACK(1)<->WHITE(2)
    uint8_t captures = 0, ni;
    FOR_NB(ni, idx)
        if(boardAt(ni) == opponent && !hasLiberty(ni, opponent))
            captures = 1;

    // Suicide: no captures and the placed group has no liberties
    if(!captures && !hasLiberty(idx, turn)) { simBoard[idx] = EMPTY; return 0; }

    // Ko (point rule, replaced the memcmp-vs-prevBoard positional check
    // 08-29 -- provably equivalent one ply back): the recapture at koCap
    // recreates the previous position exactly when it captures ONLY the
    // ko stone. If any other neighboring group dies too (snapback-plus),
    // the position differs and the move is legal, matching the old
    // whole-board compare.
    uint8_t isKo = 0;
    if(koExact && idx == koCap) {
        isKo = 1;
        FOR_NB(ni, idx) {
            if(ni == lastMove) continue;
            if(boardAt(ni) == opponent && !hasLiberty(ni, opponent)) {
                isKo = 0;
                break;
            }
        }
    }
    // Remove the trial stone (08-30 mini-gauntlet fix): nnOpeningMove
    // calls this INSIDE its candidate loop and reads simBoard for
    // features between calls -- a leftover phantom corrupted every
    // later candidate's evaluation (113 -> 60/200 vs L0).
    simBoard[idx] = EMPTY;
    return !isKo;
}

uint8_t Game::playMove(uint8_t x, uint8_t y) {
    if(!isValidMove(x, y)) return 0;

    uint8_t idx = y * BOARD_SIZE + x;
    board[idx] = turn;   // idx hoisted: set(x,y) recomputed the mul
                         // (the packed-era CSE died with packedSet)

    uint8_t opponent = 3 - turn;   // BLACK(1)<->WHITE(2)
    uint8_t captureIdx = turn - 1;   // BLACK(1)->0, WHITE(2)->1
    // Single fused neighbor walk (08-29): own/preEmpty accumulate from
    // the same packedGet the capture test needs. The old second loop
    // re-walked the neighbors POST-capture; with capTotal==1 the only
    // changed cell is capCell -- read here as opponent BEFORE its own
    // removal -- so post-capture libs == preEmpty + 1 exactly, and
    // koExact's (libs == 1) is (preEmpty == 0). Equivalence proven:
    // 4 distinct neighbors, one removed stone, own/empty cells
    // untouched by the capture.
    // Sequential removal == simultaneous evaluation (proof, 08-30): two
    // capturable opponent groups cannot be adjacent (same-color
    // adjacency = same group), so removing one never changes another's
    // liberty verdict. (A recorded validate->apply handoff measured +28
    // vs this recomputation -- state costs more than rework here.)
    uint8_t capTotal = 0, capCell = 0xFF, own = 0, preEmpty = 0, ni;
    FOR_NB(ni, idx) {
        uint8_t c = board[ni];
        if(c == turn) own = 1;
        else if(c == EMPTY) preEmpty++;
        else if(c == opponent && !hasLiberties(ni)) {
            uint8_t n = captureGroup(ni);
            captures[captureIdx] += n;
            capTotal += n;   // <= 80 ever on 9x9: cannot overflow
            capCell = ni;   // single capture => group size 1 => ni IS the cell
        }
    }

    // Last-move + ko state (see game.h): koCap = strict caps==1 cell
    // (search rootKo), koExact adds the lone-stone/one-liberty
    // conditions that make the recapture position-recreating.
    lastMove = idx;
    koCap = 0xFF;
    koExact = 0;
    if(capTotal == 1) {
        koCap = capCell;
        koExact = (own | preEmpty) == 0;  // == !own && !preEmpty
    }

    consecutivePasses = 0;
    turn = opponent;
    return 1;
}

// noinline: 3 call sites x 38B body -- dropping measured +26
// (re-bisected 08-30; the original memcpy justification died with
// prevBoard, but the economics held and grew).
__attribute__((noinline))
void Game::pass() {
    lastMove = koCap = 0xFF;
    koExact = 0;
    consecutivePasses++;
    turn = 3 - turn;
}

uint8_t Game::isGameOver() {
    return consecutivePasses >= 2;
}

void Game::computeScore() {
    // Fixpoint touch-mask propagation (Jay 08-29): BLACK=1/WHITE=2 are
    // OR-able bits (scoreWinner's mixing idiom, run to closure instead
    // of 1-ply). Stones seed their color bit; each empty accumulates
    // its neighbours' masks until stable -- the fixpoint IS the
    // region's touch set: mask==BLACK / ==WHITE is sole ownership,
    // 3 is dame. This killed the last game-side flood. Cold path
    // (scoring screens): the multi-pass sweep's cycles are irrelevant.
    area[0] = area[1] = 0;

    // The mask array IS a synced simBoard (08-29b): unpackBoard's copy
    // is exactly the seed (stones carry their color bit), the borrow
    // window is the established one (both callers run search-idle),
    // and simBoard's carry-free base gets 3-word indexed access where
    // a stack array paid 7-word runtime pointer builds (asm read).
    unpackBoard(*this);

    uint8_t changed = 1;
    while(changed) {
        changed = 0;
        for(uint8_t i = 0; i < BOARD_CELLS; i++) {
            if(board[i] != EMPTY) continue;
            uint8_t m = simBoard[i], ni;   // sequential: GCC pointer-walks
            FOR_NB(ni, i) m |= boardAt(ni);
            if(m != simBoard[i]) { simBoard[i] = m; changed = 1; }
        }
    }
    // Stones are FIXED POINTS of the mask (seeded as their color,
    // never changed), so mask==color already counts stones AND
    // territory together: the count loop reads only the masks.
    // (Mask-INDEXED bins -- area[m]++ branchless, Jay 08-30 -- measured
    // flash-wash at +2 RAM and a .bss reshuffle that broke simBoard's
    // carry bound: with only 2 read bins, the branch form IS the
    // compressed one. Receipt kept for when a consumer wants dame.)
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t m = simBoard[i];
        if(m == BLACK) area[0]++;
        else if(m == WHITE) area[1]++;
    }
}

// stonePair deleted (08-30): its last caller was ai's countStones,
// which only wanted the TOTAL -- the pair split served nobody once
// computeScore began producing areas directly.

uint8_t Game::blackWins() {
    // PRECONDITION (08-30, round 3): scored games only -- every caller
    // pre-filters resignedBy (renderGameOver branches on it first; the
    // probes never resign), so the old in-function resign check was
    // dead on all paths. Resign verdicts belong to the resign path.
    // Chinese/area (stones + territory, matches gnugo/KataGo). PURE
    // READ (08-30, round 2 of "is areaWinner optimal?"): every
    // non-resign path to GAME_OVER passes through scoreDead, whose
    // last act is computeScore, and the board cannot change between
    // SCORING and GAME_OVER -- area[] is a valid member here. The old
    // call recomputed it per rendered frame.
    // Threshold form: with kp odd, 2*aB > 2*aW + kp is exactly
    // aB - aW > kp>>1.
    return (int8_t)(area[0] - area[1]) > (int8_t)(kpieces >> 1);
}
