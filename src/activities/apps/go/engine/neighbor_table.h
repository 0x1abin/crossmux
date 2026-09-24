#pragma once
// CrossMux port: avr/pgmspace.h is AVR-only; on ESP32 the Arduino core's
// pgmspace compat layer (via Arduino.h, pulled in by game.h) provides
// PROGMEM/pgm_read_byte as plain-memory reads.
#if defined(__AVR__)
#include <avr/pgmspace.h>
#endif

// For each cell: [n0, n1, n2, n3, 0xFF-sentinel]. Neighbours first, then a
// 0xFF (255) terminator -- no stored count. Iterate until 0xFF; the entry at
// index 3 is 0xFF iff the cell has <4 neighbours (i.e. sits on the edge).
// Replaces %9 and /9 in the flood/playout hot path -- AVR has no HW divide.
// extern: C++ namespace-scope const is internal linkage, which kept this
// table invisible to game.cpp -- whose nbIndexXY recomputed neighbours
// with a mul + bounds checks per call for two years (Jay's catch, 08-29)
extern PROGMEM const uint8_t NEIGHBOR_TABLE[81 * 5];

// The one true neighbour walk (08-29): shared by ai.cpp's playout floods
// and game.cpp's rules code. lpmNext fuses the post-increment into a
// single lpm Z+ on AVR; hosts use the portable read.
static inline __attribute__((always_inline)) uint8_t nbNext(const uint8_t *&p) {
    uint8_t v;
#if defined(__AVR__) && !defined(NO_CARRY_TRICKS)
    asm("lpm %0, Z+" : "=r"(v), "=z"(p) : "1"(p));
#else
    v = pgm_read_byte(p++);
#endif
    return v;
}
#define FOR_EACH_NEIGHBOR(q, pos) \
    for(const uint8_t *_ne = NEIGHBOR_TABLE + (pos) * 5; \
        !(((q) = nbNext(_ne)) & 0x80); )   /* sign-bit sentinel */

// Cell read through the carry-free pointer trick: 3 instructions
// instead of the 4-word 16-bit index extend. REQUIRES lo8(base)+80
// to not carry -- checkmagic.sh gates this for simBoard and
// game.board on every build. Shared by the engine's floods and
// game.cpp's rules walks (08-30).
extern uint8_t simBoard[81];
#if defined(__AVR__) && !defined(NO_CARRY_TRICKS)
#define CELL_AT(base, q) __extension__({ \
    const uint8_t *_p; \
    asm("mov %A0,%1\n\t" \
        "subi %A0,lo8(-(%2))\n\t" \
        "ldi %B0,hi8(%2)" \
        : "=&d"(_p) : "r"((uint8_t)(q)), "i"(base)); \
    *_p; })
#else
#define CELL_AT(base, q) ((base)[q])
#endif
static inline __attribute__((always_inline)) uint8_t boardAt(uint8_t q) {
    return CELL_AT(simBoard, q);
}
#ifdef NEIGHBOR_TABLE_OWNER   // exactly one TU provides the data
PROGMEM const uint8_t NEIGHBOR_TABLE[81 * 5] = {
    1, 9, 255, 255, 255, 0, 2, 10, 255, 255, 1, 3, 11, 255, 255,
    2, 4, 12, 255, 255, 3, 5, 13, 255, 255, 4, 6, 14, 255, 255,
    5, 7, 15, 255, 255, 6, 8, 16, 255, 255, 7, 17, 255, 255, 255,
    10, 0, 18, 255, 255, 9, 11, 1, 19, 255, 10, 12, 2, 20, 255,
    11, 13, 3, 21, 255, 12, 14, 4, 22, 255, 13, 15, 5, 23, 255,
    14, 16, 6, 24, 255, 15, 17, 7, 25, 255, 16, 8, 26, 255, 255,
    19, 9, 27, 255, 255, 18, 20, 10, 28, 255, 19, 21, 11, 29, 255,
    20, 22, 12, 30, 255, 21, 23, 13, 31, 255, 22, 24, 14, 32, 255,
    23, 25, 15, 33, 255, 24, 26, 16, 34, 255, 25, 17, 35, 255, 255,
    28, 18, 36, 255, 255, 27, 29, 19, 37, 255, 28, 30, 20, 38, 255,
    29, 31, 21, 39, 255, 30, 32, 22, 40, 255, 31, 33, 23, 41, 255,
    32, 34, 24, 42, 255, 33, 35, 25, 43, 255, 34, 26, 44, 255, 255,
    37, 27, 45, 255, 255, 36, 38, 28, 46, 255, 37, 39, 29, 47, 255,
    38, 40, 30, 48, 255, 39, 41, 31, 49, 255, 40, 42, 32, 50, 255,
    41, 43, 33, 51, 255, 42, 44, 34, 52, 255, 43, 35, 53, 255, 255,
    46, 36, 54, 255, 255, 45, 47, 37, 55, 255, 46, 48, 38, 56, 255,
    47, 49, 39, 57, 255, 48, 50, 40, 58, 255, 49, 51, 41, 59, 255,
    50, 52, 42, 60, 255, 51, 53, 43, 61, 255, 52, 44, 62, 255, 255,
    55, 45, 63, 255, 255, 54, 56, 46, 64, 255, 55, 57, 47, 65, 255,
    56, 58, 48, 66, 255, 57, 59, 49, 67, 255, 58, 60, 50, 68, 255,
    59, 61, 51, 69, 255, 60, 62, 52, 70, 255, 61, 53, 71, 255, 255,
    64, 54, 72, 255, 255, 63, 65, 55, 73, 255, 64, 66, 56, 74, 255,
    65, 67, 57, 75, 255, 66, 68, 58, 76, 255, 67, 69, 59, 77, 255,
    68, 70, 60, 78, 255, 69, 71, 61, 79, 255, 70, 62, 80, 255, 255,
    73, 63, 255, 255, 255, 72, 74, 64, 255, 255, 73, 75, 65, 255, 255,
    74, 76, 66, 255, 255, 75, 77, 67, 255, 255, 76, 78, 68, 255, 255,
    77, 79, 69, 255, 255, 78, 80, 70, 255, 255, 79, 71, 255, 255, 255,
};


#ifdef PACKED_TRIT
// Jay's trit-coded neighbour stream (2026-08): two bytes per cell,
// interleaved. Each base-9 digit is one direction code: 1=W 2=E 3=N
// 4=S, +4 marks the cell's LAST direction, 0 = unused padding. A
// holds the first two digits, B the next two; corners encode both
// their directions in A with the last-flag set, so B is never read
// -- the lpm Z+ that fetched A already left the pointer on B for
// the cells that need it. Codes map to deltas via TRIT_DELTA.
PROGMEM const uint8_t NBR_TRIT[162] = {
    0x4A, 0x00, 0x13, 0x08, 0x13, 0x08, 0x13, 0x08, 0x13, 0x08, 0x13, 0x08, 0x13, 0x08, 0x13, 0x08, 0x49, 0x00,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x1D, 0x08, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x13, 0x4B, 0x1C, 0x08,
    0x41, 0x00, 0x13, 0x07, 0x13, 0x07, 0x13, 0x07, 0x13, 0x07, 0x13, 0x07, 0x13, 0x07, 0x13, 0x07, 0x40, 0x00,
};
PROGMEM const int8_t TRIT_DELTA[9] = {0, -1, 1, -9, 9, -1, 1, -9, 9};
#endif
#endif // PACKED_TRIT
