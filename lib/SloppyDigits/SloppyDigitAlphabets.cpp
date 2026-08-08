#include "SloppyDigitAlphabets.h"

namespace sloppy {
namespace {

// Ported verbatim from nclock/src/algorithm/alphabets.ts.
// Each digit is a sequence of cubic Bezier commands inside a DIGIT_W × DIGIT_H
// box (100 × 160). M sets the current point; C draws a cubic from the current
// point through three control points (the third becoming the new current point).
#define M(X, Y) {CmdKind::Move, (X), (Y), 0, 0, 0, 0}
#define C(X0, Y0, X1, Y1, X2, Y2) {CmdKind::Cubic, (X0), (Y0), (X1), (Y1), (X2), (Y2)}

// ─── geometric ──────────────────────────────────────────────────────────────
constexpr Cmd kGeo0[] = {M(50, 10), C(75, 10, 95, 40, 95, 80), C(95, 120, 75, 150, 50, 150), C(25, 150, 5, 120, 5, 80),
                         C(5, 40, 25, 10, 50, 10)};
constexpr Cmd kGeo1[] = {M(25, 40), C(33, 30, 42, 20, 50, 10), C(50, 55, 50, 105, 50, 150), M(25, 150),
                         C(40, 150, 60, 150, 80, 150)};
constexpr Cmd kGeo2[] = {M(10, 40), C(10, 10, 90, 5, 90, 45), C(90, 80, 10, 105, 10, 150),
                         C(35, 150, 65, 150, 90, 150)};
constexpr Cmd kGeo3[] = {M(10, 30), C(25, 5, 90, 10, 90, 55),   C(90, 78, 60, 85, 45, 85),
                         M(45, 85), C(60, 85, 95, 92, 95, 120), C(95, 155, 25, 165, 10, 135)};
constexpr Cmd kGeo4[] = {M(70, 10), C(70, 55, 70, 105, 70, 150), M(70, 10), C(50, 40, 30, 70, 10, 100),
                         C(35, 100, 65, 100, 90, 100)};
constexpr Cmd kGeo5[] = {M(85, 10), C(60, 10, 30, 10, 10, 12), C(10, 32, 10, 55, 10, 75), C(35, 65, 90, 68, 90, 110),
                         C(90, 152, 30, 162, 8, 138)};
constexpr Cmd kGeo6[] = {M(80, 15), C(55, 5, 18, 30, 10, 80), C(5, 122, 10, 158, 50, 152), C(92, 152, 95, 92, 50, 88),
                         C(22, 88, 10, 102, 10, 122)};
constexpr Cmd kGeo7[] = {M(10, 22), C(35, 18, 65, 18, 90, 15), C(72, 55, 50, 105, 30, 150)};
constexpr Cmd kGeo8[] = {M(50, 10), C(22, 10, 15, 78, 50, 82),  C(85, 78, 78, 10, 50, 10),
                         M(50, 82), C(15, 82, 5, 152, 50, 152), C(95, 152, 85, 82, 50, 82)};
constexpr Cmd kGeo9[] = {M(50, 10),
                         C(22, 10, 5, 30, 5, 55),
                         C(5, 80, 25, 92, 50, 92),
                         C(75, 92, 95, 80, 95, 55),
                         C(95, 30, 78, 10, 50, 10),
                         M(95, 55),
                         C(95, 100, 92, 132, 55, 152)};

// Bare stem extracted from kGeo1's main vertical (cmd[2]). Used when
// Style.oneIsPlain is set, giving a "naked 1" with no top hook or base.
constexpr Cmd kGeo1Plain[] = {M(50, 10), C(50, 55, 50, 105, 50, 150)};

constexpr Glyph kGeometricGlyphs[10] = {
    {kGeo0, sizeof(kGeo0) / sizeof(Cmd)}, {kGeo1, sizeof(kGeo1) / sizeof(Cmd)}, {kGeo2, sizeof(kGeo2) / sizeof(Cmd)},
    {kGeo3, sizeof(kGeo3) / sizeof(Cmd)}, {kGeo4, sizeof(kGeo4) / sizeof(Cmd)}, {kGeo5, sizeof(kGeo5) / sizeof(Cmd)},
    {kGeo6, sizeof(kGeo6) / sizeof(Cmd)}, {kGeo7, sizeof(kGeo7) / sizeof(Cmd)}, {kGeo8, sizeof(kGeo8) / sizeof(Cmd)},
    {kGeo9, sizeof(kGeo9) / sizeof(Cmd)},
};

// ─── script ─────────────────────────────────────────────────────────────────
constexpr Cmd kScr0[] = {M(55, 8), C(82, 8, 96, 42, 92, 82), C(88, 122, 72, 154, 45, 152), C(18, 150, 8, 116, 12, 76),
                         C(16, 38, 28, 10, 55, 8)};
constexpr Cmd kScr1[] = {M(22, 48), C(32, 38, 44, 24, 58, 10), C(56, 58, 54, 105, 50, 152)};
constexpr Cmd kScr2[] = {M(12, 38), C(8, 8, 78, 0, 92, 32), C(98, 60, 62, 78, 40, 100), C(22, 118, 10, 132, 6, 150),
                         C(32, 148, 62, 148, 94, 148)};
constexpr Cmd kScr3[] = {M(10, 28), C(28, 6, 82, 4, 92, 38),     C(98, 62, 60, 80, 40, 82),
                         M(40, 82), C(70, 82, 98, 100, 92, 128), C(82, 162, 22, 162, 4, 132)};
constexpr Cmd kScr4[] = {M(76, 12),
                         C(74, 56, 74, 104, 72, 152),
                         M(76, 12),
                         C(60, 30, 40, 56, 22, 80),
                         C(16, 90, 10, 100, 4, 108),
                         C(32, 106, 62, 106, 94, 106)};
constexpr Cmd kScr5[] = {M(88, 10), C(60, 6, 30, 6, 8, 14), C(10, 34, 12, 56, 16, 76), C(40, 64, 94, 70, 88, 110),
                         C(80, 152, 24, 162, 2, 132)};
constexpr Cmd kScr6[] = {M(84, 14), C(60, 0, 16, 28, 6, 78), C(-2, 124, 10, 162, 52, 152), C(94, 144, 98, 92, 50, 84),
                         C(22, 80, 8, 100, 10, 124)};
constexpr Cmd kScr7[] = {M(6, 18), C(30, 28, 78, 6, 94, 20), C(76, 50, 56, 100, 38, 152)};
constexpr Cmd kScr8[] = {M(50, 8),  C(20, 4, 12, 70, 50, 80),    C(88, 70, 80, 4, 50, 8),
                         M(50, 80), C(10, 84, -4, 154, 50, 154), C(104, 154, 90, 84, 50, 80)};
constexpr Cmd kScr9[] = {M(54, 8),
                         C(22, 6, 4, 28, 8, 56),
                         C(12, 82, 38, 92, 56, 90),
                         C(78, 88, 94, 72, 92, 48),
                         C(88, 26, 76, 8, 54, 8),
                         M(92, 48),
                         C(94, 96, 86, 134, 48, 154)};

// Bare stem extracted from kScr1's main vertical (cmd[1]).
constexpr Cmd kScr1Plain[] = {M(58, 10), C(56, 58, 54, 105, 50, 152)};

constexpr Glyph kScriptGlyphs[10] = {
    {kScr0, sizeof(kScr0) / sizeof(Cmd)}, {kScr1, sizeof(kScr1) / sizeof(Cmd)}, {kScr2, sizeof(kScr2) / sizeof(Cmd)},
    {kScr3, sizeof(kScr3) / sizeof(Cmd)}, {kScr4, sizeof(kScr4) / sizeof(Cmd)}, {kScr5, sizeof(kScr5) / sizeof(Cmd)},
    {kScr6, sizeof(kScr6) / sizeof(Cmd)}, {kScr7, sizeof(kScr7) / sizeof(Cmd)}, {kScr8, sizeof(kScr8) / sizeof(Cmd)},
    {kScr9, sizeof(kScr9) / sizeof(Cmd)},
};

// ─── marker (bulged, hand-drawn) ────────────────────────────────────────────
constexpr Cmd kMar0[] = {M(52, 6), C(88, 8, 102, 50, 94, 86), C(88, 130, 72, 158, 44, 154), C(12, 150, -2, 110, 8, 70),
                         C(18, 30, 28, 4, 52, 6)};
constexpr Cmd kMar1[] = {M(22, 50), C(36, 32, 50, 16, 62, 8), C(60, 56, 58, 105, 56, 152), M(16, 150),
                         C(40, 152, 62, 152, 88, 150)};
constexpr Cmd kMar2[] = {M(8, 40), C(4, 4, 86, -4, 96, 36), C(102, 64, 56, 80, 34, 102), C(16, 120, 6, 134, 4, 152),
                         C(32, 150, 62, 150, 96, 152)};
constexpr Cmd kMar3[] = {M(10, 28), C(28, 4, 82, 0, 96, 38),      C(104, 64, 62, 82, 38, 82),
                         M(38, 82), C(72, 80, 104, 100, 94, 132), C(82, 168, 16, 168, 0, 132)};
constexpr Cmd kMar4[] = {M(78, 8),
                         C(76, 56, 74, 104, 72, 156),
                         M(78, 8),
                         C(58, 32, 38, 60, 18, 86),
                         C(12, 94, 6, 102, 0, 110),
                         C(30, 110, 62, 110, 98, 110)};
constexpr Cmd kMar5[] = {M(92, 8), C(62, 4, 28, 4, 4, 14), C(6, 36, 10, 58, 14, 80), C(40, 64, 104, 70, 94, 116),
                         C(82, 162, 16, 168, -4, 134)};
constexpr Cmd kMar6[] = {M(86, 12), C(60, -4, 12, 28, 4, 80), C(-6, 130, 12, 168, 54, 156),
                         C(102, 144, 104, 86, 50, 80), C(20, 78, 4, 100, 10, 128)};
constexpr Cmd kMar7[] = {M(2, 16), C(28, 32, 80, 4, 98, 22), C(76, 52, 54, 104, 34, 156)};
constexpr Cmd kMar8[] = {M(50, 6),  C(16, 0, 8, 72, 50, 82),    C(92, 72, 86, 0, 50, 6),
                         M(50, 82), C(6, 84, -8, 158, 50, 158), C(108, 156, 94, 84, 50, 82)};
constexpr Cmd kMar9[] = {M(56, 6),
                         C(20, 4, -2, 30, 4, 58),
                         C(10, 86, 40, 96, 60, 92),
                         C(86, 88, 104, 70, 98, 44),
                         C(92, 22, 80, 6, 56, 6),
                         M(98, 44),
                         C(102, 100, 90, 138, 44, 158)};

// Bare stem extracted from kMar1's main vertical (cmd[2]).
constexpr Cmd kMar1Plain[] = {M(62, 8), C(60, 56, 58, 105, 56, 152)};

constexpr Glyph kMarkerGlyphs[10] = {
    {kMar0, sizeof(kMar0) / sizeof(Cmd)}, {kMar1, sizeof(kMar1) / sizeof(Cmd)}, {kMar2, sizeof(kMar2) / sizeof(Cmd)},
    {kMar3, sizeof(kMar3) / sizeof(Cmd)}, {kMar4, sizeof(kMar4) / sizeof(Cmd)}, {kMar5, sizeof(kMar5) / sizeof(Cmd)},
    {kMar6, sizeof(kMar6) / sizeof(Cmd)}, {kMar7, sizeof(kMar7) / sizeof(Cmd)}, {kMar8, sizeof(kMar8) / sizeof(Cmd)},
    {kMar9, sizeof(kMar9) / sizeof(Cmd)},
};

#undef M
#undef C

constexpr Alphabet kAlphabets[static_cast<int>(AlphabetId::Count)] = {
    {kGeometricGlyphs, {kGeo1Plain, sizeof(kGeo1Plain) / sizeof(Cmd)}, "geometric"},
    {kScriptGlyphs, {kScr1Plain, sizeof(kScr1Plain) / sizeof(Cmd)}, "script"},
    {kMarkerGlyphs, {kMar1Plain, sizeof(kMar1Plain) / sizeof(Cmd)}, "marker"},
};

}  // namespace

const Alphabet& getAlphabet(AlphabetId id) {
  const auto idx = static_cast<int>(id);
  if (idx < 0 || idx >= static_cast<int>(AlphabetId::Count)) {
    return kAlphabets[0];
  }
  return kAlphabets[idx];
}

}  // namespace sloppy
