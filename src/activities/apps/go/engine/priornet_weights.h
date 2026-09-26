#pragma once
// CrossMux port: avr/pgmspace.h is AVR-only; ESP32 Arduino core provides
// PROGMEM/pgm_read_byte via game.h's Arduino.h include.
#if defined(__AVR__)
#include <avr/pgmspace.h>
#endif
// Learned prior net (priornet): 24 -> 4 -> 1, integer-exact export.
// pre_h = sum_i (f_i - PN_FMID[i]) * PN_W1[i*4+h]   (int32, scale S1=14.1904)
// out   = sum_h relu(pre_h + PN_B1[h]*? ) ... see check_prior.py reference.
// out is a monotone int32 score; caller scales into the bonus range.
#define PN_NF 19
#define PN_H 4
PROGMEM static const int8_t PN_W1[76] = {
    2, -1, 3, -4, -3, 0, -4, -2, -2, -5, -4, -4,
    -2, 0, 2, -1, 9, 9, -7, -13, 2, 9, 0, -14,
    26, 23, 12, 14, 1, 1, 1, -1, -2, 7, 3, 8,
    -17, 29, -37, -100, 28, 35, -4, 21, -7, 9, 15, -27,
    -17, -20, 15, 23, 7, -19, 3, -37, -1, -16, -1, 3,
    -9, 39, 20, 36, -7, -8, -21, -25, 0, 0, -1, 0,
    0, -1, 4, -19,
};
PROGMEM static const int16_t PN_B1[4] = {
    16, 10, -19, 43,
};
PROGMEM static const int8_t PN_V[4] = {
    70, 83, -54, -100,
};
PROGMEM static const int8_t PN_FMID[19] = {
    0, 0, 4, 3, 3, 0, 0, 7, 7, 0, 0, 0,
    0, 0, 0, 0, 0, 28, 0,
};
// FMID as compile-time constants: ai.cpp folds these at the pf[]
// write sites (kernel reads d directly); every index must be
// emitted so a net with different medians can't silently break
// the fold.
#define PN_FM_0 0
#define PN_FM_1 0
#define PN_FM_2 4
#define PN_FM_3 3
#define PN_FM_4 3
#define PN_FM_5 0
#define PN_FM_6 0
#define PN_FM_7 7
#define PN_FM_8 7
#define PN_FM_9 0
#define PN_FM_10 0
#define PN_FM_11 0
#define PN_FM_12 0
#define PN_FM_13 0
#define PN_FM_14 0
#define PN_FM_15 0
#define PN_FM_16 0
#define PN_FM_17 28
#define PN_FM_18 0
