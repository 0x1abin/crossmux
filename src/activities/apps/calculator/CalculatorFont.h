#pragma once

#include <EpdFontFamily.h>
#include <builtinFonts/calculator_18_bold.h>
#include <builtinFonts/calculator_18_regular.h>

namespace calculator {

// Separate from legacy reader IDs, which are bound to the 12pt offline fallback.
inline constexpr int DISPLAY_FONT_ID = 0x43414C12;
inline const EpdFont displayRegularFont(&calculator_18_regular);
inline const EpdFont displayBoldFont(&calculator_18_bold);
inline const EpdFontFamily displayFontFamily(&displayRegularFont, &displayBoldFont);

}  // namespace calculator
