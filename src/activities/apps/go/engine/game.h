#pragma once

#include "constants.h"

// CrossMux port: Arduboy2 dependency removed. PROGMEM/pgm_read_byte
// compatibility comes from Arduino.h on ESP32 (pgmspace compat layer).
#ifdef ARDUINO
#include <Arduino.h>
#endif

// Shared flood-fill work stack (defined in game.cpp). Also used by the
// AI's playout engine — safe because game logic and search never run a
// flood at the same time, and no state persists between calls.
extern uint8_t floodScratch[BOARD_CELLS];

// Plain stack indexing — no 0x800 redirect. test/checkmagic.sh asserts
// at build time that floodScratch clears RAM 0x800-0x801, so a stomped
// entry can never feed a bogus board position back into a flood.
inline uint8_t& floodSlot(uint8_t i) {
    return floodScratch[i];
}

// Boards on the game side are packed 2 bits per cell (4 cells/byte):
// they are touched only on real moves and rendering, so the shift cost
// is invisible — unlike the AI's simBoard, which stays byte-per-cell
// for playout speed.
// Board snapshot size for harnesses. HISTORICAL NAME: the board went
// byte-per-cell on 08-29, so "packed" bytes == cells now; the define
// keeps every host harness's sizeof-based snapshot/restore/compare
// exact (a 21-byte value here would silently truncate them).
#define PACKED_BOARD_BYTES BOARD_CELLS

// packed accessors deleted (08-29): the board went byte-per-cell -- the
// 2-bit packing's shared accessor bodies + per-call marshalling cost
// more flash than the 60B of RAM it saved, and forced unpackBoard's
// existence. (See METHODOLOGY: representation law.)
// Byte-compat shims so host harnesses keep reading boards uniformly:
inline uint8_t packedGet(const uint8_t *b, uint8_t i) { return b[i]; }
inline void packedSet(uint8_t *b, uint8_t i, uint8_t v) { b[i] = v; }

class Game {
    public:
    uint8_t board[BOARD_CELLS];   // byte per cell (08-29)   // EMPTY, BLACK, or WHITE
    // prevBoard[21] replaced 08-29 (Jay: point-ko instead of positional
    // memcmp; provably equivalent one ply back): 3 bytes of last-move
    // state carry everything the diff loops derived.
    uint8_t lastMove; // cell of the last stone placed, 0xFF = none/pass
    uint8_t koCap;    // cell captured by the last move IF it captured
                      // exactly one stone, else 0xFF (rootKo semantics)
    uint8_t koExact;  // 1 = full simple-ko (capturing stone lone, one
                      // liberty): real-rules recapture ban at koCap
    uint8_t turn;       // BLACK or WHITE
    uint8_t mode;
    uint8_t aiPlayer;
    uint8_t captures[2]; // captures[0]=black's captures, captures[1]=white's
    uint8_t consecutivePasses;
    uint8_t cursorX, cursorY;

    // Scoring
    uint8_t area[2];      // area[0]=black, area[1]=white: stones +
                          // exclusive territory, in [0,81]. Consumers
                          // only ever wanted AREAS (display + winner),
                          // so computeScore produces them directly --
                          // the old territory[] made every consumer
                          // re-add stonePair (08-30).
    uint8_t kpieces;      // komi in half-points (default 13 = 6.5)
    uint8_t resignedBy;   // 0 = none, else the color that resigned

    // Temp buffer for flood fill (values 0-2, packed like the boards)
    // visited[] lives in ai.cpp's simBoard (borrowed byte scratch,
    // 08-29): game floods only run while the search is idle, and the
    // byte form drops the packed accessors from every flood step.

    void reset();
    uint8_t at(uint8_t x, uint8_t y);
    void set(uint8_t x, uint8_t y, uint8_t val);

    uint8_t isValidMove(uint8_t x, uint8_t y);
    uint8_t playMove(uint8_t x, uint8_t y);
    void pass();

    uint8_t hasLiberties(uint8_t start);
    uint8_t captureGroup(uint8_t start);

    void computeScore();
    uint8_t isGameOver();

    // Returns 1=black wins, 2=white wins. Chinese/AREA scoring (stones+territory,
    // not prisoners) to match gnugo, KataGo, and the engine's own playout scoreWinner.
    uint8_t blackWins();    // 1 = Black wins the SCORED game (see .cpp
                            // precondition); was areaWinner returning
                            // {BLACK,WHITE} -- the only device caller
                            // decoded that straight back to a bool
                            // (round 4 of "is areaWinner optimal?")
};
