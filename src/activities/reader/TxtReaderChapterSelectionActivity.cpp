#include "TxtReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int TxtReaderChapterSelectionActivity::getPageItems() const {
  return UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
}

std::string TxtReaderChapterSelectionActivity::chapterTitle(const int index) {
  txt_chapter_index::Record chapter;
  if (index < 0 || !txt.readChapter(chapterFile, chapterCount, static_cast<uint32_t>(index), chapter)) return {};
  return chapter.title;
}

void TxtReaderChapterSelectionActivity::selectChapter() {
  txt_chapter_index::Record chapter;
  if (selectorIndex < 0 || !txt.readChapter(chapterFile, chapterCount, static_cast<uint32_t>(selectorIndex), chapter)) {
    return;
  }
  setResult(TxtOffsetResult{chapter.sourceOffset});
  finish();
}

void TxtReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  if (!txt.openChapterIndex(chapterFile, encoding, chapterCount)) {
    LOG_ERR("TRC", "Failed to open TXT chapter index");
    chapterCount = 0;
  } else if (chapterCount > 0) {
    uint32_t currentChapter = 0;
    if (txt.findChapterForOffset(chapterFile, chapterCount, currentOffset, currentChapter)) {
      selectorIndex = static_cast<int>(currentChapter);
    }
  }
  requestUpdate();
}

void TxtReaderChapterSelectionActivity::onExit() {
  Activity::onExit();
  chapterFile.close();
}

void TxtReaderChapterSelectionActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  const int totalItems = static_cast<int>(chapterCount);
  if (totalItems == 0) return;
  const int pageItems = getPageItems();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  switch (handleListTouch(selectorIndex, totalItems, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      selectChapter();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectChapter();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void TxtReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const Rect content{screen.x, contentTop, screen.width, contentHeight};
  if (chapterCount == 0) {
    UITheme::drawCenteredWrappedText(renderer, content, UI_10_FONT_ID, tr(STR_NO_CHAPTERS), 2);
  } else {
    GUI.drawList(renderer, content, static_cast<int>(chapterCount), selectorIndex,
                 [this](const int index) { return chapterTitle(index); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
