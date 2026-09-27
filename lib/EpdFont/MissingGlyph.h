#pragma once

#include <BidiUtils.h>
#include <Utf8.h>

#include <algorithm>

#include "EpdFontData.h"

namespace missingGlyph {

inline bool isCombining(const uint32_t cp) { return utf8IsCombiningMark(cp) || BidiUtils::isTransparentMark(cp); }

// Missing whitespace, controls and default-ignorable formatters have no ink.
inline bool isInvisible(const uint32_t cp) {
  return cp <= 0x20 || (cp >= 0x7F && cp <= 0xA0) || cp == 0xAD || cp == 0x34F || cp == 0x61C || cp == 0x1680 ||
         (cp >= 0x180B && cp <= 0x180F) || (cp >= 0x2000 && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202F) ||
         (cp >= 0x205F && cp <= 0x206F) || cp == 0x3000 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF ||
         (cp >= 0xFFF9 && cp <= 0xFFFB) || (cp >= 0x1BCA0 && cp <= 0x1BCA3) || (cp >= 0x1D173 && cp <= 0x1D17A) ||
         (cp >= 0xE0000 && cp <= 0xE0FFF);
}

// No bitmap or allocation: callers draw the outline using these same metrics.
inline EpdGlyph metrics(const int ascender, const uint32_t cp) {
  if (isInvisible(cp) || isCombining(cp)) return {};
  const auto side = static_cast<uint8_t>(std::clamp(ascender, 0, 340) * 3 / 4);
  const uint8_t size = std::max<uint8_t>(4, side);
  return {size, size, static_cast<uint16_t>((size + 2) * 16), 1, size, 0, 0};
}

}  // namespace missingGlyph
