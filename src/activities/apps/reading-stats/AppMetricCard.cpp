#include "AppMetricCard.h"

#include <algorithm>
#include <vector>

#include "fontIds.h"

namespace {
void drawCheckBadge(const GfxRenderer& renderer, const int x, const int y) {
  renderer.fillRect(x, y, 18, 18, true);
  renderer.drawLine(x + 4, y + 10, x + 7, y + 13, 2, false);
  renderer.drawLine(x + 7, y + 13, x + 13, y + 5, 2, false);
}
}  // namespace

namespace AppMetricCard {

void draw(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
          const Options& options) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int textX = rect.x + options.paddingX;
  const int textWidth = rect.width - options.contentInset;

  const int valueFontId =
      options.shrinkValue && renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) > textWidth
          ? UI_10_FONT_ID
          : UI_12_FONT_ID;
  const std::string truncatedValue = renderer.truncatedText(valueFontId, value.c_str(), textWidth, EpdFontFamily::BOLD);

  // Resolve the label line(s) up front so the value+label block can be vertically
  // centered in the card from font metrics — callers never hand-tune Y offsets, so
  // cards of any height stay balanced.
  std::vector<std::string> labelLines;
  if (options.labelMode == LabelMode::Wrap) {
    labelLines = renderer.wrappedText(UI_10_FONT_ID, label, textWidth, options.labelMaxLines, EpdFontFamily::REGULAR);
  } else if (options.labelMode == LabelMode::Truncate) {
    labelLines.push_back(renderer.truncatedText(UI_10_FONT_ID, label, textWidth, EpdFontFamily::REGULAR));
  } else {  // LabelMode::Simple
    labelLines.emplace_back(label);
  }
  if (labelLines.empty()) {
    labelLines.emplace_back("");
  }

  const int valueLineHeight = renderer.getLineHeight(valueFontId);
  const int labelLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int blockHeight = valueLineHeight + static_cast<int>(labelLines.size()) * labelLineHeight;
  const int blockTop = rect.y + std::max(0, (rect.height - blockHeight) / 2);

  renderer.drawText(valueFontId, textX, blockTop, truncatedValue.c_str(), true, EpdFontFamily::BOLD);

  int labelTop = blockTop + valueLineHeight;
  for (const auto& line : labelLines) {
    renderer.drawText(UI_10_FONT_ID, textX, labelTop, line.c_str());
    labelTop += labelLineHeight;
  }

  if (options.showCheck) {
    drawCheckBadge(renderer, rect.x + rect.width - 28, rect.y + 40);
  }
}

}  // namespace AppMetricCard
