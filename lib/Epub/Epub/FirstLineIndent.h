#pragma once

#include <cstdint>

// Reader-level first-line indent control. Three states so the stock
// behaviour (keep the book's own CSS text-indent) is the default and the
// user can force indent or force no indent explicitly.
namespace FirstLineIndent {
constexpr uint8_t Auto = 0;      // Keep the book's CSS text-indent (or none).
constexpr uint8_t Indent = 1;    // Force two CJK characters / three spaces.
constexpr uint8_t NoIndent = 2;  // Force no indent, overriding book CSS.
}  // namespace FirstLineIndent
