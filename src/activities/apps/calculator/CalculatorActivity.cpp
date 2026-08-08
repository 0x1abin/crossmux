#include "CalculatorActivity.h"

#include <I18n.h>

#include <algorithm>
#include <array>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"

namespace {

constexpr int kColumns = 4;
constexpr int kRows = 5;
constexpr int kKeyCount = kColumns * kRows;
constexpr int kInitialSelection = 9;
constexpr int kMinimumRoundedKeyGap = 4;
constexpr int kMinimumSectionSpacing = 12;

constexpr std::array<calculator::Key, kKeyCount> kKeys = {
    calculator::Key::Clear,  calculator::Key::ToggleSign, calculator::Key::Percent,   calculator::Key::Divide,
    calculator::Key::Digit7, calculator::Key::Digit8,     calculator::Key::Digit9,    calculator::Key::Multiply,
    calculator::Key::Digit4, calculator::Key::Digit5,     calculator::Key::Digit6,    calculator::Key::Subtract,
    calculator::Key::Digit1, calculator::Key::Digit2,     calculator::Key::Digit3,    calculator::Key::Add,
    calculator::Key::Digit0, calculator::Key::Decimal,    calculator::Key::Backspace, calculator::Key::Equals,
};

static_assert(kKeys[kInitialSelection] == calculator::Key::Digit5);

struct CalculatorLayout {
  Rect header;
  Rect display;
  Rect grid;
  int keyWidth;
  int keyHeight;
  int gap;
};

CalculatorLayout layoutFor(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sectionSpacing = std::max(metrics.verticalSpacing, kMinimumSectionSpacing);
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  int viewTop, viewRight, viewBottom, viewLeft;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  const int left = safe.x + std::max(metrics.contentSidePadding, viewLeft);
  const int top = safe.y + std::max(metrics.topPadding, viewTop);
  const int right = safe.x + safe.width - std::max(metrics.contentSidePadding, viewRight);
  const int bottom = safe.y + safe.height - std::max(metrics.verticalSpacing, viewBottom);
  const int width = std::max(0, right - left);
  const Rect header{left, top, width, metrics.headerHeight};

  const int displayTop = header.y + header.height + metrics.verticalSpacing / 2;
  const int availableHeight = std::max(0, bottom - displayTop);
  const int displayHeight = std::max(112, availableHeight / 4);
  const Rect displayArea{left, displayTop, width, displayHeight};

  const int gridTop = displayArea.y + displayArea.height + sectionSpacing;
  const int gap = metrics.optionPopupSelectionRadius > 0
                      ? std::max({kMinimumRoundedKeyGap, metrics.menuSpacing, metrics.keyboardKeySpacing})
                      : 0;
  const int availableGridHeight = std::max(0, bottom - gridTop);
  const int keyWidth = std::max(1, (width - gap * (kColumns - 1)) / kColumns);
  const int keyHeight = std::max(1, (availableGridHeight - gap * (kRows - 1)) / kRows);
  const int gridWidth = keyWidth * kColumns + gap * (kColumns - 1);
  const int gridHeight = keyHeight * kRows + gap * (kRows - 1);
  const Rect grid{left + (width - gridWidth) / 2, gridTop, gridWidth, gridHeight};
  return {header, displayArea, grid, keyWidth, keyHeight, gap};
}

Rect keyRect(const CalculatorLayout& layout, const int index) {
  const int row = index / kColumns;
  const int column = index % kColumns;
  return Rect{layout.grid.x + column * (layout.keyWidth + layout.gap),
              layout.grid.y + row * (layout.keyHeight + layout.gap), layout.keyWidth, layout.keyHeight};
}

int keyFromPoint(const CalculatorLayout& layout, const int x, const int y) {
  for (int index = 0; index < kKeyCount; ++index) {
    const Rect key = keyRect(layout, index);
    if (x >= key.x && x < key.x + key.width && y >= key.y && y < key.y + key.height) return index;
  }
  return -1;
}

const char* keyLabel(const calculator::Key key) {
  switch (key) {
    case calculator::Key::Digit0:
      return "0";
    case calculator::Key::Digit1:
      return "1";
    case calculator::Key::Digit2:
      return "2";
    case calculator::Key::Digit3:
      return "3";
    case calculator::Key::Digit4:
      return "4";
    case calculator::Key::Digit5:
      return "5";
    case calculator::Key::Digit6:
      return "6";
    case calculator::Key::Digit7:
      return "7";
    case calculator::Key::Digit8:
      return "8";
    case calculator::Key::Digit9:
      return "9";
    case calculator::Key::Decimal:
      return ".";
    case calculator::Key::Add:
      return "+";
    case calculator::Key::Subtract:
      return "-";
    case calculator::Key::Multiply:
      return "×";
    case calculator::Key::Divide:
      return "÷";
    case calculator::Key::Percent:
      return "%";
    case calculator::Key::ToggleSign:
      return "±";
    case calculator::Key::Backspace:
      return "";
    case calculator::Key::Clear:
      return "AC";
    case calculator::Key::Equals:
      return "=";
    case calculator::Key::Count:
      return "";
  }
  return "";
}

void drawBackspaceIcon(const GfxRenderer& renderer, const Rect key, const bool black) {
  const int centerX = key.x + key.width / 2;
  const int centerY = key.y + key.height / 2;
  const int left = centerX - 13;
  const int right = centerX + 13;
  const int top = centerY - 9;
  const int bottom = centerY + 9;
  renderer.drawLine(left, centerY, left + 8, top, 2, black);
  renderer.drawLine(left + 8, top, right, top, 2, black);
  renderer.drawLine(right, top, right, bottom, 2, black);
  renderer.drawLine(right, bottom, left + 8, bottom, 2, black);
  renderer.drawLine(left + 8, bottom, left, centerY, 2, black);
  renderer.drawLine(centerX + 1, centerY - 5, centerX + 9, centerY + 5, 2, black);
  renderer.drawLine(centerX + 9, centerY - 5, centerX + 1, centerY + 5, 2, black);
}

}  // namespace

void CalculatorActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  state_.reset();
  selected_ = kInitialSelection;
  requestUpdate();
}

void CalculatorActivity::onExit() { Activity::onExit(); }

void CalculatorActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }

  const CalculatorLayout layout = layoutFor(renderer);
  int touchX, touchY;
  if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
    const int touched = keyFromPoint(layout, touchX, touchY);
    if (touched >= 0 && touched != selected_) {
      RenderLock lock(*this);
      selected_ = touched;
      lock.unlock();
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int touched = keyFromPoint(layout, touchX, touchY);
    if (touched >= 0) {
      RenderLock lock(*this);
      selected_ = touched;
      state_.press(kKeys[selected_]);
      lock.unlock();
      requestUpdate();
    }
    return;
  }

  int next = selected_;
  if (mappedInput.wasReleased(MappedInputManager::Button::ScreenLeft)) {
    next = selected_ / kColumns * kColumns + (selected_ % kColumns + kColumns - 1) % kColumns;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenRight)) {
    next = selected_ / kColumns * kColumns + (selected_ % kColumns + 1) % kColumns;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenUp)) {
    next = (selected_ / kColumns + kRows - 1) % kRows * kColumns + selected_ % kColumns;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::ScreenDown)) {
    next = (selected_ / kColumns + 1) % kRows * kColumns + selected_ % kColumns;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    RenderLock lock(*this);
    state_.press(kKeys[selected_]);
    lock.unlock();
    requestUpdate();
    return;
  }

  if (next != selected_) {
    RenderLock lock(*this);
    selected_ = next;
    lock.unlock();
    requestUpdate();
  }
}

void CalculatorActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sectionSpacing = std::max(metrics.verticalSpacing, kMinimumSectionSpacing);
  const CalculatorLayout layout = layoutFor(renderer);
  GUI.drawHeader(renderer, layout.header, tr(STR_CALCULATOR_TITLE));

  const char* expression = state_.expressionText();
  const int expressionWidth = renderer.getTextWidth(UI_12_FONT_ID, expression);
  const int expressionY = layout.display.y + sectionSpacing;
  renderer.drawText(UI_12_FONT_ID, layout.display.x + layout.display.width - expressionWidth, expressionY, expression);

  const char* result = state_.hasError() ? tr(STR_CALCULATOR_ERROR) : state_.resultText();
  const int resultWidth = renderer.getTextWidth(NOTOSANS_18_FONT_ID, result, EpdFontFamily::BOLD);
  const int resultLineHeight = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int resultY = layout.display.y + layout.display.height - resultLineHeight - sectionSpacing;
  renderer.drawText(NOTOSANS_18_FONT_ID, layout.display.x + layout.display.width - resultWidth, resultY, result, true,
                    EpdFontFamily::BOLD);

  const EpdFontFamily::Style keyStyle =
      metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  for (int index = 0; index < kKeyCount; ++index) {
    const Rect key = keyRect(layout, index);
    const int radius = std::min(metrics.optionPopupSelectionRadius, std::min(key.width, key.height) / 2);
    bool foregroundBlack = true;
    if (index == selected_) foregroundBlack = GUI.drawSelectionBackground(renderer, key);
    renderer.drawRoundedRect(key.x, key.y, key.width, key.height, 1, radius, true);

    if (kKeys[index] == calculator::Key::Backspace) {
      drawBackspaceIcon(renderer, key, foregroundBlack);
      continue;
    }
    const char* label = keyLabel(kKeys[index]);
    const int textWidth = renderer.getTextWidth(NOTOSANS_18_FONT_ID, label, keyStyle);
    const int textHeight = renderer.getTextHeight(NOTOSANS_18_FONT_ID);
    renderer.drawText(NOTOSANS_18_FONT_ID, key.x + (key.width - textWidth) / 2, key.y + (key.height - textHeight) / 2,
                      label, foregroundBlack, keyStyle);
  }

  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT),
                                                       tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
