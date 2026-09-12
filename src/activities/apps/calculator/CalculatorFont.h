#pragma once

#include <EpdFontFamily.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_regular.h>

namespace calculator {

// Separate from legacy reader IDs, which are bound to the 12pt offline fallback.
inline constexpr int DISPLAY_FONT_ID = 0x43414C12;
inline const EpdFont displayRegularFont(&notosans_18_regular);
inline const EpdFont displayBoldFont(&notosans_18_bold);
inline const EpdFontFamily displayFontFamily(&displayRegularFont, &displayBoldFont);

}  // namespace calculator
