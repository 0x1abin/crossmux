#pragma once

#include <GfxRenderer.h>

#include <string>

#include "components/themes/BaseTheme.h"

// A gray "metric" card: a bold value on top with a label beneath it, both left
// aligned. The value+label block is vertically centered within the card from font
// line heights, so cards of ANY height stay balanced. Callers pick horizontal
// padding, how the label is fitted, and decorations only — never vertical offsets.
// (Do not re-introduce per-caller value/label Y options; that is what caused each
// page to hand-tune magic numbers. Adjust the card height instead.)
namespace AppMetricCard {

enum class LabelMode {
  Simple,    // draw the label as-is
  Truncate,  // single line, ellipsized to fit the card width
  Wrap,      // up to labelMaxLines wrapped lines
};

struct Options {
  int paddingX = 12;        // left inset for the value and label
  int contentInset = 24;    // value/label fit width = rect.width - contentInset
  int labelMaxLines = 2;    // max wrapped label lines (LabelMode::Wrap)
  bool shrinkValue = true;  // drop the value to the small font if it would overflow
  bool showCheck = false;   // draw a goal-met check badge in the top-right corner
  LabelMode labelMode = LabelMode::Wrap;
};

void draw(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
          const Options& options = Options{});

}  // namespace AppMetricCard
