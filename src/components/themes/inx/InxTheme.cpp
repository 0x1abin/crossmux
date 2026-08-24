#include "InxTheme.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "I18n.h"
#include "InxItemLayout.h"
#include "components/UITheme.h"
#include "components/icons/inx_apps.h"
#include "components/icons/inx_tabs.h"
#include "fontIds.h"

namespace {
constexpr int kIconSize = 38;
constexpr int kUnderlineHeight = 5;
constexpr int kRowHeight = 66;
constexpr int kRowPadding = 20;
constexpr int kListIconSize = 24;
constexpr int kMenuIconSize = 32;
constexpr int kIconGap = 10;
constexpr int kMaxValueWidth = 200;
constexpr int kSideHintY = 345;
constexpr int kX3SideHintY = 155;
constexpr int kHintWidth = 80;
constexpr int kHintBarHeight = 5;
constexpr int kHintGap = 4;

void drawHintText(const GfxRenderer& renderer, const char* label, const int x, const int contentY,
                  const int contentBottom) {
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  const int textY = std::max(contentY, contentBottom - renderer.getLineHeight(SMALL_FONT_ID) + 1);
  const GfxRenderer::ClipScope clip(renderer, x + 1, contentY, kHintWidth - 2, contentBottom - contentY + 1);
  renderer.drawText(SMALL_FONT_ID, x + std::max(1, (kHintWidth - textWidth) / 2), textY, label);
}

const char* hintLabel(const char* label) {
  if (std::strcmp(label, tr(STR_DIR_LEFT)) == 0) return "<";
  if (std::strcmp(label, tr(STR_DIR_RIGHT)) == 0) return ">";
  return label;
}

const uint8_t* iconForTab(const MainTab tab) {
  switch (tab) {
    case MainTab::Recent:
      return InxRecentTabIcon;
    case MainTab::Library:
      return InxLibraryTabIcon;
    case MainTab::Settings:
      return InxSettingsTabIcon;
    case MainTab::Statistics:
      return InxStatisticsTabIcon;
    case MainTab::Apps:
      return InxAppsTabIcon;
    case MainTab::None:
      return nullptr;
  }
  return nullptr;
}

void drawInxIcon(const GfxRenderer& renderer, const uint8_t* icon, const int x, const int y) {
  constexpr int rowBytes = (kIconSize + 7) / 8;
  for (int row = 0; row < kIconSize; ++row) {
    for (int column = 0; column < kIconSize; ++column) {
      const uint8_t byte = icon[row * rowBytes + column / 8];
      if (((byte >> (7 - column % 8)) & 1U) == 0) renderer.drawPixel(x + column, y + row, true);
    }
  }
}

void drawDottedSeparator(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}
}  // namespace

void InxTheme::drawHeader(const GfxRenderer& renderer, const Rect rect, const char* title, const char* subtitle) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryX = rect.x + rect.width - 12 - InxMetrics::values.batteryWidth;
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, InxMetrics::values.batteryWidth, InxMetrics::values.batteryHeight},
                   showBatteryPercentage);

  const int titleTop = rect.y + InxMetrics::values.batteryBarHeight;
  const int rightPadding = InxMetrics::values.contentSidePadding;
  int titleRight = rect.x + rect.width - rightPadding;
  if (subtitle && *subtitle) {
    const int subtitleWidth =
        std::min(renderer.getTextWidth(SMALL_FONT_ID, subtitle), std::max(0, rect.width / 2 - rightPadding));
    titleRight -= subtitleWidth + kIconGap;
    if (subtitleWidth > 0) {
      const GfxRenderer::ClipScope clip(renderer, titleRight + kIconGap, titleTop, subtitleWidth,
                                        renderer.getLineHeight(SMALL_FONT_ID));
      renderer.drawText(
          SMALL_FONT_ID, rect.x + rect.width - rightPadding - subtitleWidth,
          titleTop +
              std::max(0, (renderer.getLineHeight(NOTOSERIF_12_FONT_ID) - renderer.getLineHeight(SMALL_FONT_ID)) / 2),
          subtitle);
    }
  }

  if (title && *title && titleRight > rect.x + kRowPadding) {
    const GfxRenderer::ClipScope clip(renderer, rect.x + kRowPadding, titleTop, titleRight - rect.x - kRowPadding,
                                      renderer.getLineHeight(NOTOSERIF_12_FONT_ID));
    renderer.drawText(NOTOSERIF_12_FONT_ID, rect.x + kRowPadding, titleTop, title, true, EpdFontFamily::BOLD);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

void InxTheme::drawSubHeader(const GfxRenderer& renderer, const Rect rect, const char* label,
                             const char* rightLabel) const {
  const int padding = kRowPadding;
  int labelRight = rect.x + rect.width - padding;
  if (rightLabel && *rightLabel) {
    const int rightWidth =
        std::min(renderer.getTextWidth(UI_10_FONT_ID, rightLabel), std::max(0, rect.width / 2 - padding));
    labelRight -= rightWidth + kIconGap;
    if (rightWidth > 0) {
      const GfxRenderer::ClipScope clip(renderer, labelRight + kIconGap, rect.y, rightWidth, rect.height - 1);
      renderer.drawText(UI_10_FONT_ID, rect.x + rect.width - padding - rightWidth,
                        rect.y + std::max(0, (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2), rightLabel);
    }
  }
  if (label && *label && labelRight > rect.x + padding) {
    const GfxRenderer::ClipScope clip(renderer, rect.x + padding, rect.y, labelRight - rect.x - padding,
                                      rect.height - 1);
    renderer.drawText(UI_10_FONT_ID, rect.x + padding,
                      rect.y + std::max(0, (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2), label, true,
                      EpdFontFamily::BOLD);
  }
  drawDottedSeparator(renderer, rect.x, rect.y + rect.height - 1, rect.width);
}

void InxTheme::drawTabBar(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                          const bool) const {
  if (tabs.empty()) return;
  const int count = static_cast<int>(tabs.size());
  for (int index = 0; index < count; ++index) {
    const int left = rect.x + rect.width * index / count;
    const int right = rect.x + rect.width * (index + 1) / count;
    const bool active = tabs[index].selected;
    if (active) renderer.fillRect(left, rect.y, right - left, rect.height - 1, true);
    const int textWidth =
        renderer.getTextWidth(UI_10_FONT_ID, tabs[index].label, active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int textX = left + std::max(0, (right - left - textWidth) / 2);
    const GfxRenderer::ClipScope clip(renderer, left, rect.y, right - left, rect.height - 1);
    renderer.drawText(UI_10_FONT_ID, textX,
                      rect.y + std::max(0, (rect.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2),
                      tabs[index].label, !active, active ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}

bool InxTheme::tabIndexFromPoint(const GfxRenderer&, const Rect rect, const std::vector<TabInfo>& tabs, const int x,
                                 const int y, int& index) const {
  if (tabs.empty() || x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) return false;
  index = (x - rect.x) * static_cast<int>(tabs.size()) / rect.width;
  return true;
}

void InxTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const {
  if (!buttonHintsVisible()) return;

  const GfxRenderer::Orientation original = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonHeight = InxMetrics::values.buttonHintsHeight;
  const int pageWidth = renderer.getScreenWidth();
  const int margin = gpio.deviceIsX3() ? 65 : 58;
  const int gap = gpio.deviceIsX3() ? 12 : 8;
  const int positions[] = {margin, margin + kHintWidth + gap, pageWidth - margin - kHintWidth * 2 - gap,
                           pageWidth - margin - kHintWidth};
  const char* labels[] = {btn1, btn2, btn3, btn4};
  const int hintY = pageHeight - buttonHeight;
  const int barY = pageHeight - kHintBarHeight;
  const int contentBottom = barY - kHintGap - 1;

  for (int i = 0; i < 4; ++i) {
    renderer.fillRect(positions[i], hintY, kHintWidth, buttonHeight, false);
    if (!labels[i] || !*labels[i]) continue;
    renderer.fillRectDither(positions[i], barY, kHintWidth, kHintBarHeight, Color::DarkGray);
    drawHintText(renderer, hintLabel(labels[i]), positions[i], hintY, contentBottom);
  }
  renderer.setOrientation(original);
}

void InxTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  if (gpio.hasTouch()) return;
  constexpr int width = InxMetrics::values.sideButtonHintsWidth;
  constexpr int height = 78;
  const int screenWidth = renderer.getScreenWidth();
  const char* labels[] = {topBtn, bottomBtn};
  const int xs[] = {gpio.deviceIsX3() ? 0 : screenWidth - width, screenWidth - width};
  const int firstY = gpio.deviceIsX3() ? kX3SideHintY : kSideHintY;
  const int ys[] = {firstY, gpio.deviceIsX3() ? firstY : firstY + height + 5};

  for (int i = 0; i < 2; ++i) {
    if (!labels[i] || !*labels[i]) continue;
    renderer.fillRect(xs[i], ys[i], width, height, false);
    renderer.drawRect(xs[i], ys[i], width, height, true);
    const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    renderer.drawTextRotated90CW(SMALL_FONT_ID, xs[i], ys[i] + (height + textWidth) / 2, labels[i]);
  }
}

int InxTheme::getListRowStep(const bool) const { return kRowHeight; }

int InxTheme::getListPageItems(const int contentHeight, const bool) const {
  return std::max(1, contentHeight / kRowHeight);
}

void InxTheme::drawList(const GfxRenderer& renderer, const Rect rect, const int itemCount, const int selectedIndex,
                        const std::function<std::string(int index)>& rowTitle,
                        const std::function<std::string(int index)>& rowSubtitle,
                        const std::function<UIIcon(int index)>& rowIcon,
                        const std::function<std::string(int index)>& rowValue, const bool,
                        const std::function<bool(int index)>& rowDimmed, const bool showSelection,
                        const std::function<bool(int index)>& rowHeading) const {
  if (itemCount <= 0 || rect.height <= 0) return;

  const int pageItems = getListPageItems(rect.height, rowSubtitle != nullptr);
  const int pageStart = selectedIndex >= 0 ? selectedIndex / pageItems * pageItems : 0;
  const int pageEnd = std::min(itemCount, pageStart + pageItems);
  const bool hasScrollBar = itemCount > pageItems;
  const int contentRight = rect.x + rect.width - (hasScrollBar ? 10 : 0);
  const int iconSize = rowSubtitle != nullptr ? kMenuIconSize : kListIconSize;
  const bool selectionVisible = showSelection && UITheme::getInstance().showSelectionCursor();

  for (int index = pageStart; index < pageEnd; ++index) {
    const int slot = index - pageStart;
    const int rowY = rect.y + slot * kRowHeight;
    const bool selected = selectionVisible && index == selectedIndex;
    if (selected) renderer.fillRect(rect.x, rowY, rect.width, kRowHeight, true);

    int textX = rect.x + kRowPadding;
    if (rowIcon) {
      if (const uint8_t* bitmap = iconForName(rowIcon(index), iconSize)) {
        const int iconY = rowY + (kRowHeight - iconSize) / 2;
        if (selected)
          renderer.drawIconInverted(bitmap, textX, iconY, iconSize);
        else
          renderer.drawIcon(bitmap, textX, iconY, iconSize);
        textX += iconSize + kIconGap;
      }
    }

    std::string value;
    int valueWidth = 0;
    if (rowValue) {
      value = renderer.truncatedText(UI_10_FONT_ID, rowValue(index).c_str(), kMaxValueWidth);
      valueWidth = value.empty() ? 0 : renderer.getTextWidth(UI_10_FONT_ID, value.c_str()) + kIconGap;
    }
    const bool heading = rowHeading && rowHeading(index);
    const int titleFont = heading ? UI_12_FONT_ID : UI_10_FONT_ID;
    const auto titleStyle = heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = std::max(1, contentRight - kRowPadding - textX - valueWidth);
    const std::string title = renderer.truncatedText(titleFont, rowTitle(index).c_str(), textWidth, titleStyle);
    const int titleY = rowY + (rowSubtitle ? 12 : (kRowHeight - renderer.getLineHeight(titleFont)) / 2);
    renderer.drawText(titleFont, textX, titleY, title.c_str(), !selected, titleStyle);

    if (rowSubtitle) {
      const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, rowSubtitle(index).c_str(), textWidth);
      renderer.drawText(SMALL_FONT_ID, textX, rowY + 36, subtitle.c_str(), !selected);
    }
    if (!value.empty()) {
      const int valueX = contentRight - kRowPadding - renderer.getTextWidth(UI_10_FONT_ID, value.c_str());
      const int valueY = rowY + (kRowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, valueX, valueY, value.c_str(), !selected);
    }
    if (rowDimmed && rowDimmed(index) && !selected) {
      drawDitherMask(renderer, textX, rowY, std::max(0, contentRight - textX), kRowHeight - 1);
    }
    drawDottedSeparator(renderer, rect.x, rowY + kRowHeight - 1, rect.width);
  }

  drawSideScrollBar(renderer, rect, itemCount, pageStart, pageItems);
}

void InxTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount, const int selectedIndex,
                              const std::function<std::string(int index)>& buttonLabel,
                              const std::function<UIIcon(int index)>& rowIcon, const int) const {
  const int pageItems = InxMenuGeometry::pageItems(rect.height);
  const int pageStart = InxMenuGeometry::pageStart(selectedIndex, buttonCount, rect.height);
  const int pageEnd = std::min(buttonCount, pageStart + pageItems);
  const bool selectionVisible = UITheme::getInstance().showSelectionCursor();
  for (int index = pageStart; index < pageEnd; ++index) {
    const int rowY = rect.y + (index - pageStart) * kRowHeight;
    const bool selected = selectionVisible && index == selectedIndex;
    if (selected) renderer.fillRect(rect.x, rowY, rect.width, kRowHeight, true);

    int textX = rect.x + kRowPadding;
    if (rowIcon) {
      const UIIcon icon = rowIcon(index);
      if (InxAppIcons::get(icon)) {
        const int iconY = rowY + (kRowHeight - kMenuIconSize) / 2;
        InxAppIcons::draw(renderer, icon, textX, iconY, 1, selected);
        textX += kMenuIconSize + kIconGap;
      } else if (const uint8_t* bitmap = iconForName(icon, kMenuIconSize)) {
        const int iconY = rowY + (kRowHeight - kMenuIconSize) / 2;
        if (selected)
          renderer.drawIconInverted(bitmap, textX, iconY, kMenuIconSize);
        else
          renderer.drawIcon(bitmap, textX, iconY, kMenuIconSize);
        textX += kMenuIconSize + kIconGap;
      }
    }

    const std::string label = renderer.truncatedText(UI_12_FONT_ID, buttonLabel(index).c_str(),
                                                     std::max(1, rect.x + rect.width - kRowPadding - textX));
    const int textY = rowY + (kRowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, label.c_str(), !selected);
    drawDottedSeparator(renderer, rect.x, rowY + kRowHeight - 1, rect.width);
  }
  drawSideScrollBar(renderer, rect, buttonCount, pageStart, pageItems);
}

void InxTheme::drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                               const int selectedIndex) const {
  if (options.empty()) return;

  const int optionCount = static_cast<int>(options.size());
  const int selected = std::clamp(selectedIndex, 0, optionCount - 1);
  const int visibleRows = InxOptionGeometry::visibleRows(optionCount);
  const int maxStart = optionCount - visibleRows;
  const int start = InxOptionGeometry::start(selected, optionCount);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int panelWidth = std::max(1, std::min(screenWidth - 24, 360));
  const int panelHeight = InxOptionGeometry::headerHeight + visibleRows * InxOptionGeometry::rowHeight;
  const int panelX = (screenWidth - panelWidth) / 2;
  const int panelY = std::max(0, (screenHeight - panelHeight) / 2);
  const bool selectionVisible = UITheme::getInstance().showSelectionCursor();

  renderer.fillRect(panelX, panelY, panelWidth, panelHeight, false);
  const std::string shownTitle = renderer.truncatedText(UI_10_FONT_ID, title, panelWidth - 32, EpdFontFamily::BOLD);
  const int titleY = panelY + (InxOptionGeometry::headerHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, panelX + 16, titleY, shownTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(panelX, panelY + InxOptionGeometry::headerHeight, panelX + panelWidth - 1,
                    panelY + InxOptionGeometry::headerHeight, true);

  for (int slot = 0; slot < visibleRows; ++slot) {
    const int optionIndex = start + slot;
    const int rowY = panelY + InxOptionGeometry::headerHeight + slot * InxOptionGeometry::rowHeight;
    const bool isSelected = selectionVisible && optionIndex == selected;
    if (isSelected) renderer.fillRect(panelX + 2, rowY, panelWidth - 4, InxOptionGeometry::rowHeight, true);
    const std::string option = renderer.truncatedText(UI_10_FONT_ID, options[optionIndex].c_str(), panelWidth - 44);
    const int textY = rowY + (InxOptionGeometry::rowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, panelX + 18, textY, option.c_str(), !isSelected,
                      isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    if (slot + 1 < visibleRows)
      drawDottedSeparator(renderer, panelX + 2, rowY + InxOptionGeometry::rowHeight - 1, panelWidth - 4);
  }

  if (optionCount > visibleRows) {
    const int trackX = panelX + panelWidth - 10;
    const int trackY = panelY + InxOptionGeometry::headerHeight;
    const int trackHeight = visibleRows * InxOptionGeometry::rowHeight;
    const int thumbHeight = std::max(8, trackHeight * visibleRows / optionCount);
    const int thumbY = trackY + start * (trackHeight - thumbHeight) / maxStart;
    renderer.fillRect(trackX, trackY, 2, trackHeight, true);
    renderer.fillRect(trackX - 2, thumbY, 6, thumbHeight, true);
  }

  renderer.drawRect(panelX, panelY, panelWidth, panelHeight, true);
  renderer.drawRect(panelX + 1, panelY + 1, panelWidth - 2, panelHeight - 2, true);
}

void InxTheme::drawMainTabBar(const GfxRenderer& renderer, const Rect rect, const MainTab selected) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const int tabCount = static_cast<int>(MainTabs::values.size());
  const int iconY = rect.y + std::max(0, (rect.height - kIconSize) / 2);

  for (size_t i = 0; i < MainTabs::values.size(); ++i) {
    const MainTab tab = MainTabs::values[i];
    const int left = rect.x + rect.width * static_cast<int>(i) / tabCount;
    const int right = rect.x + rect.width * (static_cast<int>(i) + 1) / tabCount;
    const int iconX = left + (right - left - kIconSize) / 2;
    if (const uint8_t* icon = iconForTab(tab)) drawInxIcon(renderer, icon, iconX, iconY);
    if (tab == selected) {
      renderer.fillRect(iconX, rect.y + rect.height - kUnderlineHeight, kIconSize, kUnderlineHeight);
    }
  }

  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1, true);
}
