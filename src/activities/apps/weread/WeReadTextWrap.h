#pragma once

#include <string>
#include <vector>

class GfxRenderer;

namespace WeReadTextWrap {

// CJK-aware word wrap. Splits CJK at every codepoint, Latin at spaces; falls
// back to a hard break for over-long Latin runs. Honors `\n` as a hard line
// break. Collapses leading whitespace on each soft-wrapped line.
//
// maxLines <= 0  : return all wrapped lines uncapped (use for full-text views).
// maxLines  > 0  : return at most that many lines. If more would have been
//                  produced, the last returned line ends with U+2026 ("…"),
//                  trimmed to fit within maxWidth.
std::vector<std::string> wrap(const GfxRenderer& renderer, int fontId, const std::string& text, int maxWidth,
                              int maxLines = 0);

}  // namespace WeReadTextWrap
