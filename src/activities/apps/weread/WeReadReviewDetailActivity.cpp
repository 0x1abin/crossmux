#include "WeReadReviewDetailActivity.h"

#include <I18n.h>

#include <algorithm>
#include <utility>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "WeReadTextWrap.h"

WeReadReviewDetailActivity::WeReadReviewDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string bookTitle, std::string content,
                                                       std::string footerText)
    : Activity("WeReadReviewDetail", renderer, mappedInput),
      bookTitle_(std::move(bookTitle)),
      content_(std::move(content)),
      footerText_(std::move(footerText)) {}

int WeReadReviewDetailActivity::maxScrollOffset() const {
  return std::max(0, static_cast<int>(lines_.size()) - visibleLines_);
}

void WeReadReviewDetailActivity::rewrap() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int contentWidth = sw - 2 * metrics.contentSidePadding;
  lines_ = WeReadTextWrap::wrap(renderer, UI_12_FONT_ID, content_, contentWidth);
}

void WeReadReviewDetailActivity::onEnter() {
  Activity::onEnter();
  rewrap();
  scrollOffset_ = 0;
  requestUpdate();
}

void WeReadReviewDetailActivity::loop() {
  // Page-flip: each press jumps a full visible page (visibleLines_ rows).
  // visibleLines_ is computed during render(); the first press before any
  // render falls back to a 1-line step.
  buttonNavigator.onPrevious([this] {
    const int step = std::max(1, visibleLines_);
    const int next = std::max(0, scrollOffset_ - step);
    if (next != scrollOffset_) {
      scrollOffset_ = next;
      requestUpdate();
    }
  });
  buttonNavigator.onNext([this] {
    const int step = std::max(1, visibleLines_);
    const int next = std::min(maxScrollOffset(), scrollOffset_ + step);
    if (next != scrollOffset_) {
      scrollOffset_ = next;
      requestUpdate();
    }
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void WeReadReviewDetailActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, bookTitle_.c_str(),
                 tr(STR_WEREAD_REVIEW_DETAIL_TITLE));

  const int footerStripH = footerText_.empty() ? 0 : 22;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = sh - metrics.buttonHintsHeight - metrics.verticalSpacing - footerStripH;
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

  if (!footerText_.empty()) {
    const int labelW = renderer.getTextWidth(UI_10_FONT_ID, footerText_.c_str());
    const int labelX = sw - metrics.contentSidePadding - labelW;
    const int labelY = sh - metrics.buttonHintsHeight - metrics.verticalSpacing - 16;
    renderer.drawText(UI_10_FONT_ID, labelX, labelY, footerText_.c_str(), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
