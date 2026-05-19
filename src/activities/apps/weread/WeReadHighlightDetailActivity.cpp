#include "WeReadHighlightDetailActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "WeReadTextWrap.h"

WeReadHighlightDetailActivity::WeReadHighlightDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                             std::string bookTitle, ItemSource source,
                                                             int initialIndex)
    : Activity("WeReadHighlightDetail", renderer, mappedInput),
      bookTitle_(std::move(bookTitle)),
      source_(source),
      currentIndex_(initialIndex) {}

const std::string& WeReadHighlightDetailActivity::currentText() const {
  static const std::string kEmpty;
  if (!source_.getText || currentIndex_ < 0 || currentIndex_ >= source_.itemCount) return kEmpty;
  return source_.getText(source_.ctx, currentIndex_);
}

int WeReadHighlightDetailActivity::currentCount() const {
  if (!source_.getCount || currentIndex_ < 0 || currentIndex_ >= source_.itemCount) return -1;
  return source_.getCount(source_.ctx, currentIndex_);
}

int WeReadHighlightDetailActivity::maxScrollOffset() const {
  return std::max(0, static_cast<int>(lines_.size()) - visibleLines_);
}

void WeReadHighlightDetailActivity::rewrap() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int contentWidth = sw - 2 * metrics.contentSidePadding;
  lines_ = WeReadTextWrap::wrap(renderer, UI_12_FONT_ID, currentText(), contentWidth);
}

void WeReadHighlightDetailActivity::switchTo(int newIndex) {
  if (newIndex < 0 || newIndex >= source_.itemCount) return;
  currentIndex_ = newIndex;
  scrollOffset_ = 0;
  rewrap();
  requestUpdate();
}

void WeReadHighlightDetailActivity::onEnter() {
  Activity::onEnter();
  if (currentIndex_ < 0) currentIndex_ = 0;
  if (source_.itemCount > 0 && currentIndex_ >= source_.itemCount) currentIndex_ = source_.itemCount - 1;
  rewrap();
  scrollOffset_ = 0;
  requestUpdate();
}

void WeReadHighlightDetailActivity::loop() {
  buttonNavigator.onPreviousPress([this] {
    pressStartedAtTop_ = (scrollOffset_ == 0);
    if (scrollOffset_ > 0) {
      --scrollOffset_;
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousContinuous([this] {
    if (scrollOffset_ > 0) {
      --scrollOffset_;
      requestUpdate();
    }
  });
  buttonNavigator.onPreviousRelease([this] {
    if (pressStartedAtTop_ && currentIndex_ > 0) {
      switchTo(currentIndex_ - 1);
    }
    pressStartedAtTop_ = false;
  });

  buttonNavigator.onNextPress([this] {
    const int maxOffset = maxScrollOffset();
    pressStartedAtEnd_ = (visibleLines_ > 0 && scrollOffset_ >= maxOffset);
    if (!pressStartedAtEnd_) {
      ++scrollOffset_;
      requestUpdate();
    }
  });
  buttonNavigator.onNextContinuous([this] {
    const int maxOffset = maxScrollOffset();
    if (visibleLines_ > 0 && scrollOffset_ < maxOffset) {
      ++scrollOffset_;
      requestUpdate();
    }
  });
  buttonNavigator.onNextRelease([this] {
    if (pressStartedAtEnd_ && currentIndex_ + 1 < source_.itemCount) {
      switchTo(currentIndex_ + 1);
    }
    pressStartedAtEnd_ = false;
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void WeReadHighlightDetailActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, bookTitle_.c_str(),
                 tr(STR_WEREAD_HIGHLIGHT_DETAIL_TITLE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  // Reserve a small strip above the button hints for the "N 人划线" label when
  // applicable. Keeps the line-height math stable regardless of orientation.
  const int displayCount = currentCount();
  const int countStripHeight = (displayCount >= 0) ? 22 : 0;
  const int contentBottom = sh - metrics.buttonHintsHeight - metrics.verticalSpacing - countStripHeight;
  const int contentHeight = contentBottom - contentTop;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  visibleLines_ = (lineHeight > 0) ? (contentHeight / lineHeight) : 0;

  const int textX = metrics.contentSidePadding;
  int y = contentTop;
  const int firstLine = scrollOffset_;
  const int lastLine = std::min(firstLine + visibleLines_, static_cast<int>(lines_.size()));
  for (int i = firstLine; i < lastLine; ++i) {
    renderer.drawText(UI_12_FONT_ID, textX, y, lines_[i].c_str(), true);
    y += lineHeight;
  }

  // Scrollbar on the right when content overflows.
  if (static_cast<int>(lines_.size()) > visibleLines_ && visibleLines_ > 0) {
    const int scrollAreaY = contentTop;
    const int scrollAreaH = contentHeight;
    const int total = static_cast<int>(lines_.size());
    const int barH = std::max(8, (scrollAreaH * visibleLines_) / total);
    const int maxOffset = total - visibleLines_;
    const int barY = scrollAreaY + (maxOffset > 0 ? ((scrollAreaH - barH) * scrollOffset_) / maxOffset : 0);
    const int barX = sw - 5;
    renderer.drawLine(barX, scrollAreaY, barX, scrollAreaY + scrollAreaH, true);
    renderer.fillRect(barX - 4, barY, 4, barH, true);
  }

  // Bottom count label (only when this item has a non-negative count badge).
  if (displayCount >= 0) {
    static char countBuf[24];
    std::snprintf(countBuf, sizeof(countBuf), tr(STR_WEREAD_HIGHLIGHT_COUNT_FMT), displayCount);
    const int labelW = renderer.getTextWidth(SMALL_FONT_ID, countBuf);
    const int labelX = sw - metrics.contentSidePadding - labelW;
    const int labelY = sh - metrics.buttonHintsHeight - metrics.verticalSpacing - 16;
    renderer.drawText(SMALL_FONT_ID, labelX, labelY, countBuf, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
