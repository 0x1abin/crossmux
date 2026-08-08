#pragma once

#include "SloppyDigits.h"

namespace sloppy {

constexpr int16_t DIGIT_W = 100;
constexpr int16_t DIGIT_H = 160;

enum class CmdKind : uint8_t {
  Move = 0,
  Cubic = 1,
};

struct Cmd {
  CmdKind kind;
  int16_t x0, y0;
  int16_t x1, y1;
  int16_t x2, y2;
};

struct Glyph {
  const Cmd* cmds;
  uint8_t cmdCount;
};

struct Alphabet {
  const Glyph* glyphs;
  Glyph one_plain;
  const char* name;
};

const Alphabet& getAlphabet(AlphabetId id);

}  // namespace sloppy
