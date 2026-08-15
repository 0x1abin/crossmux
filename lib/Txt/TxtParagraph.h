#pragma once

#include <cstddef>
#include <cstdint>

namespace txt_paragraph {

enum class LineKind : uint8_t { Blank, Indented, Plain };

struct LineInfo {
  LineKind kind;
  size_t contentOffset;
};

inline LineInfo analyzeLine(const uint8_t* data, const size_t length) {
  size_t offset = 0;
  while (offset < length) {
    if (data[offset] == ' ' || data[offset] == '\t' || data[offset] == '\r') {
      ++offset;
      continue;
    }
    if (offset + 2 < length && data[offset] == 0xE3 && data[offset + 1] == 0x80 && data[offset + 2] == 0x80) {
      offset += 3;
      continue;
    }
    break;
  }

  if (offset == length) return {LineKind::Blank, offset};
  return {offset > 0 ? LineKind::Indented : LineKind::Plain, offset};
}

class State {
  bool paragraphStartPending_;

 public:
  explicit State(const bool atFileStart) : paragraphStartPending_(atFileStart) {}

  void noteBlankLine() { paragraphStartPending_ = true; }

  bool consume(const LineKind kind) {
    const bool shouldIndent = paragraphStartPending_ || kind == LineKind::Indented;
    paragraphStartPending_ = false;
    return shouldIndent;
  }
};

}  // namespace txt_paragraph
