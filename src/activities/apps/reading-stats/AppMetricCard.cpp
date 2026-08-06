#include "AppMetricCard.h"

#include <algorithm>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
void drawDottedLine(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}

void drawDottedCard(const GfxRenderer& renderer, const Rect& rect) {
  if (rect.width <= 0 || rect.height <= 0) return;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  drawDottedLine(renderer, rect.x, rect.y, rect.width);
  drawDottedLine(renderer, rect.x, rect.y + rect.height - 1, rect.width);
  for (int py = rect.y; py < rect.y + rect.height; py += 3) {
    renderer.drawPixel(rect.x, py, true);
    renderer.drawPixel(rect.x + rect.width - 1, py, true);
  }
}

void drawCheckBadge(const GfxRenderer& renderer, const int x, const int y) {
  renderer.fillRect(x, y, 18, 18, true);
  renderer.drawLine(x + 4, y + 10, x + 7, y + 13, 2, false);
  renderer.drawLine(x + 7, y + 13, x + 13, y + 5, 2, false);
}
}  // namespace

namespace AppMetricCard {

void draw(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
          const Options& options) {
  if (UITheme::getInstance().hasMainTabs()) {
    drawDottedCard(renderer, rect);
  } else {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }

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

bool drawSelectablePanel(const GfxRenderer& renderer, const Rect& rect, const bool selected, const bool darkSelected,
                         const bool ditherUnselected) {
  if (UITheme::getInstance().hasMainTabs()) {
    if (selected) {
      renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
      return false;
    }
    drawDottedCard(renderer, rect);
    return true;
  }

  if (selected && darkSelected) {
    renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, 6, Color::Black);
    return false;
  }
  if (!selected && ditherUnselected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  if (selected) renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, selected ? 2 : 1, true);
  return true;
}

bool drawListRow(const GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (UITheme::getInstance().hasMainTabs()) {
    if (selected) renderer.fillRect(rect.x, rect.y, rect.width, rect.height, true);
    drawDottedLine(renderer, rect.x, rect.y + rect.height - 1, rect.width);
    return !selected;
  }
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  }
  return true;
}

void drawProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t percent) {
  if (rect.width <= 0 || rect.height <= 0) return;
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int fillWidth = std::max(0, rect.width - 4) * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4));
}

void drawListScrollBar(const GfxRenderer& renderer, const Rect& rect, const int itemCount, const int pageStart,
                       const int pageItems) {
  if (UITheme::getInstance().hasMainTabs()) GUI.drawSideScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

UIIcon menuIcon(const UIIcon icon) { return UITheme::getInstance().hasMainTabs() ? icon : UIIcon::None; }

}  // namespace AppMetricCard
