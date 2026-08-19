#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace focusReading {

inline constexpr size_t BOLD_PERCENT = 45;
inline constexpr size_t MAX_BOLD_CODEPOINTS = 9;
inline constexpr size_t LEAD_WRAP_LINES = 2;

constexpr size_t boldPrefixLength(const size_t codepointCount) {
  if (codepointCount == 0) return 0;
  return std::clamp((codepointCount * BOLD_PERCENT) / 100, size_t{1}, MAX_BOLD_CODEPOINTS);
}

constexpr bool isHanIdeograph(const uint32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

constexpr bool shouldEmphasizeCjkLead(const bool enabled, const bool firstLinePending, const int firstLineIndent,
                                      const uint32_t firstCodepoint) {
  return enabled && firstLinePending && firstLineIndent > 0 && isHanIdeograph(firstCodepoint);
}

constexpr int leadInsetForLine(const bool enabled, const size_t lineIndex, const int leadAdvance) {
  return enabled && lineIndex > 0 && lineIndex < LEAD_WRAP_LINES ? leadAdvance : 0;
}

}  // namespace focusReading
