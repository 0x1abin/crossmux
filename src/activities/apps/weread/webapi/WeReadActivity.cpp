#include "WeReadActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FontCacheManager.h>
#include <FreeInkUIGfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <optional>
#include <string>

#include "CrossPointState.h"
#include "NetworkStartup.h"
#include "SilentRestart.h"
#include "WeReadBrowseActivity.h"
#include "activities/apps/weread/WeReadTouchGeometry.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"
#include "util/QrUtils.h"

namespace {

static_assert(sizeof(WeReadClient::Operation) <= 8 * 1024, "WeRead workspace exceeds its fixed heap budget");

enum class ManageAction : uint8_t { Refresh, ClearCache, Logout };

struct ManageEntry {
  StrId title;
  ManageAction action;
};

constexpr ManageEntry kManageEntries[] = {
    {StrId::STR_WEREAD_MENU_REFRESH, ManageAction::Refresh},
    {StrId::STR_WEREAD_MENU_CLEAR_CACHE, ManageAction::ClearCache},
    {StrId::STR_WEREAD_MENU_LOGOUT, ManageAction::Logout},
};

constexpr StrId kDisclaimerParagraphs[] = {
    StrId::STR_WEREAD_DISCLAIMER_PARAGRAPH_1,
    StrId::STR_WEREAD_DISCLAIMER_PARAGRAPH_2,
    StrId::STR_WEREAD_DISCLAIMER_PARAGRAPH_3,
};

constexpr StrId kDisclaimerActions[] = {
    StrId::STR_WEREAD_DISCLAIMER_CANCEL,
    StrId::STR_WEREAD_DISCLAIMER_CONFIRM,
};

constexpr StrId kCacheScopeOptions[] = {
    StrId::STR_WEREAD_CACHE_WHOLE_BOOK,
    StrId::STR_WEREAD_CACHE_CHAPTER_RANGE,
};

constexpr StrId kShelfRefreshOptions[] = {
    StrId::STR_NO,
    StrId::STR_YES,
};

constexpr StrId kPostProcessWaitingLines[] = {
    StrId::STR_WEREAD_POST_PROCESS_WAIT_LINE_1,
    StrId::STR_WEREAD_POST_PROCESS_WAIT_LINE_2,
};

constexpr StrId kPostProcessLongWaitLines[] = {
    StrId::STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_1,
    StrId::STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_2,
    StrId::STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_3,
    StrId::STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_4,
};

constexpr int kDisclaimerActionCount = static_cast<int>(sizeof(kDisclaimerActions) / sizeof(kDisclaimerActions[0]));
constexpr int kDisclaimerParagraphCount =
    static_cast<int>(sizeof(kDisclaimerParagraphs) / sizeof(kDisclaimerParagraphs[0]));
constexpr int kMinimumDisclaimerActionGap = 4;
constexpr int kManageEntryCount = static_cast<int>(sizeof(kManageEntries) / sizeof(kManageEntries[0]));
constexpr size_t kMainTabCount = 2;
constexpr int kDetailCoverWidth = 96;
constexpr int kDetailCoverHeight = 140;
constexpr int kPortraitShelfColumns = 3;
constexpr int kPortraitShelfRows = 3;
constexpr int kLandscapeShelfColumns = 5;
constexpr int kLandscapeShelfRows = 2;
constexpr unsigned long kShelfPageHoldMs = 700;
constexpr int kNoShelfSelection = -1;

constexpr int disclaimerActionGap(const int width, const int themeSpacing) {
  return std::min(std::max(kMinimumDisclaimerActionGap, themeSpacing), std::max(0, width - kDisclaimerActionCount));
}

static_assert(disclaimerActionGap(200, 0) == kMinimumDisclaimerActionGap);
static_assert(disclaimerActionGap(200, 16) == 16);
static_assert(disclaimerActionGap(kDisclaimerActionCount, 0) == 0);

constexpr int previousShelfIndexOrTab(const int currentIndex) {
  return currentIndex > 0 ? currentIndex - 1 : kNoShelfSelection;
}

static_assert(previousShelfIndexOrTab(0) == kNoShelfSelection);
static_assert(previousShelfIndexOrTab(4) == 3);

constexpr bool canIncrementShelfFrame(const int frameSelection, const int frameItemsPerPage, const int selectedIndex,
                                      const int itemsPerPage) {
  return frameItemsPerPage == itemsPerPage && frameSelection >= 0 && selectedIndex >= 0 &&
         frameSelection != selectedIndex && itemsPerPage > 0 &&
         frameSelection / itemsPerPage == selectedIndex / itemsPerPage;
}

static_assert(!canIncrementShelfFrame(kNoShelfSelection, 9, 0, 9));
static_assert(!canIncrementShelfFrame(3, 9, 3, 9));
static_assert(canIncrementShelfFrame(3, 9, 4, 9));
static_assert(!canIncrementShelfFrame(8, 9, 9, 9));
static_assert(!canIncrementShelfFrame(3, 9, 4, 10));

WeReadShelfGridLayout shelfGridLayout(GfxRenderer& renderer, const Rect& content, const int sidePadding,
                                      const int spacing) {
  WeReadShelfGridLayout layout;
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int minimumGap = std::max(4, spacing / 2);
  const bool landscape = renderer.getOrientation() == GfxRenderer::Orientation::LandscapeClockwise ||
                         renderer.getOrientation() == GfxRenderer::Orientation::LandscapeCounterClockwise;
  layout.columns = landscape ? kLandscapeShelfColumns : kPortraitShelfColumns;
  layout.rows = landscape ? kLandscapeShelfRows : kPortraitShelfRows;
  layout.itemsPerPage = layout.columns * layout.rows;
  layout.titleGap = minimumGap;
  layout.availableX = content.x + sidePadding;
  layout.availableWidth = std::max(1, content.width - sidePadding * 2);
  const int availableHeight = std::max(1, content.height);
  const int maxCoverWidth = std::max(1, (layout.availableWidth - minimumGap * (layout.columns + 1)) / layout.columns);
  const int maxCoverHeight =
      std::max(1, (availableHeight - minimumGap * (layout.rows + 1)) / layout.rows - layout.titleGap - titleHeight);
  const float scale = std::min({1.0f, static_cast<float>(maxCoverWidth) / WeReadStore::kCoverThumbWidth,
                                static_cast<float>(maxCoverHeight) / WeReadStore::kCoverThumbHeight});
  layout.coverWidth = std::max(1, static_cast<int>(WeReadStore::kCoverThumbWidth * scale));
  layout.coverHeight = std::max(1, static_cast<int>(WeReadStore::kCoverThumbHeight * scale));
  layout.itemHeight = layout.coverHeight + layout.titleGap + titleHeight;
  layout.columnGap = std::max(0, (layout.availableWidth - layout.columns * layout.coverWidth) / (layout.columns + 1));
  layout.rowGap = std::max(0, (availableHeight - layout.rows * layout.itemHeight) / (layout.rows + 1));
  return layout;
}

bool drawCachedCover(GfxRenderer& renderer, const std::string& bookDir, const Rect& bounds) {
  const std::string path = WeReadStore::coverPath(bookDir);
  if (!Storage.exists(path.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForRead("WR", path, file)) return false;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;

  const float scale = std::min({1.0f, static_cast<float>(bounds.width) / bitmap.getWidth(),
                                static_cast<float>(bounds.height) / bitmap.getHeight()});
  const int width = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
  const int height = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
  renderer.drawBitmap(bitmap, bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2,
                      bounds.width, bounds.height);
  return true;
}

void drawProgressStatus(GfxRenderer& renderer, const Rect& content, const char* title, const char* status,
                        const uint32_t completed, const uint32_t total) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);
  const int barBlockHeight =
      total > 0 ? sectionGap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight) : 0;
  const int groupHeight = titleHeight + relatedGap + lineHeight + barBlockHeight;
  int y = content.y + std::max(0, (content.height - groupHeight) / 2);
  UITheme::drawCenteredText(renderer, content, UI_12_FONT_ID, y, title, true, EpdFontFamily::BOLD);
  y += titleHeight + relatedGap;
  UITheme::drawCenteredText(renderer, content, UI_10_FONT_ID, y, status);
  if (total == 0) return;

  const int sidePadding = std::min(metrics.contentSidePadding, content.width / 4);
  GUI.drawProgressBar(renderer,
                      Rect{content.x + sidePadding, y + lineHeight + sectionGap,
                           std::max(1, content.width - sidePadding * 2), metrics.progressBarHeight},
                      completed, total);
}

struct Utf8Glyph {
  char text[5] = {};
  uint8_t fileBytes = 0;
  uint8_t textBytes = 0;
};

bool readUtf8Glyph(HalFile& file, uint32_t remaining, Utf8Glyph& glyph) {
  glyph = {};
  if (remaining == 0) return false;
  const int first = file.read();
  if (first < 0) return false;
  glyph.fileBytes = 1;
  const auto lead = static_cast<uint8_t>(first);
  int expected = 1;
  if ((lead & 0xE0) == 0xC0) {
    expected = 2;
  } else if ((lead & 0xF0) == 0xE0) {
    expected = 3;
  } else if ((lead & 0xF8) == 0xF0) {
    expected = 4;
  }
  if (expected == 1 && lead >= 0x80) {
    glyph.text[0] = '?';
    glyph.textBytes = 1;
    return true;
  }
  glyph.text[0] = static_cast<char>(lead);
  glyph.textBytes = 1;
  for (int i = 1; i < expected; ++i) {
    if (glyph.fileBytes >= remaining) {
      glyph.text[0] = '?';
      glyph.text[1] = '\0';
      glyph.textBytes = 1;
      return true;
    }
    const int next = file.read();
    if (next < 0) return false;
    ++glyph.fileBytes;
    if ((next & 0xC0) != 0x80) {
      glyph.text[0] = '?';
      glyph.text[1] = '\0';
      glyph.textBytes = 1;
      return true;
    }
    glyph.text[glyph.textBytes++] = static_cast<char>(next);
  }
  glyph.text[glyph.textBytes] = '\0';
  return true;
}

void logHeap([[maybe_unused]] const char* phase) {
  LOG_DBG("WR", "%s: free=%u largest=%u stack=%u", phase, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

class WeReadChapterRangeActivity final : public Activity {
 public:
  WeReadChapterRangeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, WeReadClient::Operation& operation,
                             const int chapterCount)
      : Activity("WeReadChapterRange", renderer, mappedInput), operation_(operation), chapterCount_(chapterCount) {}

  void onEnter() override {
    Activity::onEnter();
    logHeap("chapter range selector");
    requestUpdate();
  }

  void loop() override {
    if (readFailed_.load()) {
      setResult(ActivityResult{});
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }

    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect content = SubpageLayout::contentRect(screen, metrics);
    const int pageItems = GUI.getListPageItems(content.height, false);
    switch (handleListTouch(selectedIndex_, chapterCount_, content.y, content.height, false)) {
      case ListTouchResult::Activated:
        selectCurrent();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, chapterCount_, pageItems);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, chapterCount_, pageItems);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      selectCurrent();
      return;
    }

    buttonNavigator_.onNextRelease([this] {
      selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, chapterCount_);
      requestUpdate();
    });
    buttonNavigator_.onPreviousRelease([this] {
      selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, chapterCount_);
      requestUpdate();
    });
    buttonNavigator_.onNextContinuous([this, pageItems] {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, chapterCount_, pageItems);
      requestUpdate();
    });
    buttonNavigator_.onPreviousContinuous([this, pageItems] {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, chapterCount_, pageItems);
      requestUpdate();
    });
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();

    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                   I18N.get(stageTitle()));

    GUI.drawList(
        renderer, SubpageLayout::contentRect(screen, metrics), chapterCount_, selectedIndex_,
        [this](const int index) { return rowTitle(index); }, nullptr, nullptr,
        [this](const int index) {
          return stage_ == Stage::End && index == firstIndex_ ? std::string(tr(STR_WEREAD_CACHE_RANGE_START_MARK))
                                                              : std::string();
        },
        false, [this](const int index) { return stage_ == Stage::End && index < firstIndex_; });

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }

 private:
  enum class Stage : uint8_t { Start, End };

  WeReadClient::Operation& operation_;
  ButtonNavigator buttonNavigator_;
  WeReadStore::TocRecord rowRecord_;
  std::atomic<bool> readFailed_{false};
  int chapterCount_ = 0;
  int selectedIndex_ = 0;
  int firstIndex_ = 0;
  Stage stage_ = Stage::Start;

  StrId stageTitle() const {
    switch (stage_) {
      case Stage::Start:
        return StrId::STR_WEREAD_CACHE_RANGE_START;
      case Stage::End:
        return StrId::STR_WEREAD_CACHE_RANGE_END;
    }
    return StrId::STR_WEREAD_CACHE_RANGE_START;
  }

  std::string rowTitle(const int index) {
    if (readFailed_.load()) return {};
    if (!operation_.readChapter(static_cast<uint32_t>(index), rowRecord_) ||
        !memchr(rowRecord_.title, '\0', sizeof(rowRecord_.title))) {
      readFailed_.store(true);
      return {};
    }
    char text[sizeof(rowRecord_.title) + 16];
    snprintf(text, sizeof(text), "%u. %s", static_cast<unsigned>(index + 1),
             rowRecord_.title[0] ? rowRecord_.title : tr(STR_UNNAMED));
    return text;
  }

  void selectCurrent() {
    switch (stage_) {
      case Stage::Start:
        firstIndex_ = selectedIndex_;
        stage_ = Stage::End;
        requestUpdate();
        return;
      case Stage::End:
        if (selectedIndex_ < firstIndex_) return;
        setResult(ChapterRangeResult{static_cast<uint32_t>(firstIndex_), static_cast<uint32_t>(selectedIndex_)});
        finish();
        return;
    }
  }
};

static_assert(sizeof(WeReadChapterRangeActivity) <= 1024, "WeRead chapter selector exceeds its fixed heap budget");

}  // namespace

void WeReadActivity::onEnter() {
  Activity::onEnter();
  if (!WeReadBrowse::clearLegacyWorkspace()) LOG_ERR("WR", "legacy browse workspace cleanup failed");
  NetworkStartup::prepare(renderer);
  disclaimerSelected_ = 0;
  disclaimerSaveFailed_ = false;
  manageSelected_ = 0;
  shelfSelected_.store(0);
  shelfFrameInvalidated_.store(true);
  mainTab_.store(MainTab::Shelf);
  mainFocus_.store(MainFocus::Content);
  // drawTabBar requires a vector; reserve its fixed 16-byte ESP32-C3 payload
  // once for the Activity lifetime instead of allocating in the render path.
  mainTabs_.clear();
  mainTabs_.reserve(kMainTabCount);
  mainTabs_.push_back({tr(STR_WEREAD_TAB_SHELF), true});
  mainTabs_.push_back({tr(STR_WEREAD_TAB_MANAGE), false});
  resetShelfCoverLoading();
  detailSelected_ = 0;
  introPage_ = 0;
  introPageCount_ = 1;
  detail_ = {};
  detailLoaded_ = false;
  detailLoadFailed_ = false;
  detailOptionsKnown_ = false;
  detailIntroTruncated_ = false;
  introPagesTruncated_ = false;
  downloadChapterScope_ = WeReadClient::DownloadOptions::ChapterScope::WholeBook;
  optionPopupClosing_ = false;
  wifiSessionActive_ = false;
  wifiReleasePending_ = false;
  syncShelfCoverScope_ = WeReadClient::Operation::ShelfCoverScope::None;
  LOG_DBG("WR", "onEnter activity=%u free=%u largest=%u", static_cast<unsigned>(sizeof(*this)),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (!WeReadStore::hasAcceptedDisclaimer()) {
    state_.store(State::Disclaimer);
    requestUpdate();
    return;
  }
  enterApp();
}

void WeReadActivity::enterApp() {
  disclaimerSaveFailed_ = false;
  // This bounded 832-byte probe is gone before TLS and avoids a transient heap
  // allocation that could fragment the ESP32-C3 heap.
  WeReadStore::Session session;
  const bool loggedIn = WeReadStore::loadSession(session);
  session.clear();
  if (loggedIn) {
    openShelf();
  } else {
    syncShelf();
  }
}

void WeReadActivity::onExit() {
  operation_.reset();
  downloadRenderPending_.store(false);
  stageRenderPending_.store(false);
  if (shelfFile_.isOpen()) shelfFile_.close();
  std::vector<TabInfo>().swap(mainTabs_);
  if (wifiSessionActive_ && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    esp_wifi_deinit();
  }
  LOG_DBG("WR", "onExit free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  Activity::onExit();
}

bool WeReadActivity::refreshShelf() {
  shelfFrameInvalidated_.store(true);
  if (shelfFile_.isOpen()) shelfFile_.close();
  shelfCount_ = 0;
  if (!WeReadStore::openShelf(shelfFile_, shelfCount_)) {
    if (shelfFile_.isOpen()) shelfFile_.close();
    return false;
  }
  if (shelfCount_ == 0) {
    shelfSelected_.store(0);
  } else if (shelfSelected_.load() >= static_cast<int>(shelfCount_)) {
    shelfSelected_.store(static_cast<int>(shelfCount_ - 1));
  }
  resetShelfCoverLoading();
  return true;
}

bool WeReadActivity::readShelf(const int index, WeReadStore::ShelfRecord& record) const {
  return index >= 0 && static_cast<uint32_t>(index) < shelfCount_ &&
         WeReadStore::readShelfRecord(shelfFile_, static_cast<uint32_t>(index), record);
}

Rect WeReadActivity::contentBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return SubpageLayout::contentRect(safe, metrics);
}

Rect WeReadActivity::mainContentBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return SubpageLayout::contentRect(UITheme::getInstance().getScreenSafeArea(renderer, true, false), metrics, true);
}

Rect WeReadActivity::detailActionsBounds(const Rect& content) const {
  const int height = kDetailListActionCount * GUI.getListRowStep(false);
  return Rect{content.x, content.y + content.height - height, content.width, height};
}

Rect WeReadActivity::detailIntroductionBounds(const Rect& content) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect actions = detailActionsBounds(content);
  const int y = content.y + kDetailCoverHeight + metrics.verticalSpacing;
  return Rect{content.x + metrics.contentSidePadding, y, content.width - metrics.contentSidePadding * 2,
              std::max(1, actions.y - metrics.verticalSpacing - y)};
}

Rect WeReadActivity::disclaimerSafeBounds() const {
  return UITheme::getInstance().getScreenSafeArea(renderer, true, false);
}

Rect WeReadActivity::disclaimerContentBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return SubpageLayout::contentRect(disclaimerSafeBounds(), metrics);
}

Rect WeReadActivity::disclaimerActionsBounds() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = disclaimerContentBounds();
  const int minimumHeight = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int height = std::min(content.height, std::max(GUI.getListRowStep(false), minimumHeight));
  const int availableWidth = std::max(0, content.width - metrics.contentSidePadding * 2);
  const int labelWidth =
      std::accumulate(std::begin(kDisclaimerActions), std::end(kDisclaimerActions), 0, [this](int width, StrId action) {
        return std::max(width, renderer.getTextWidth(UI_10_FONT_ID, I18N.get(action)));
      });
  const int gap = disclaimerActionGap(availableWidth, metrics.verticalSpacing);
  const int targetWidth =
      (labelWidth + metrics.contentSidePadding * 2) * kDisclaimerActionCount + gap * (kDisclaimerActionCount - 1);
  const int width = std::min(availableWidth, targetWidth);
  const int bottomY = content.y + content.height - height;
  const int cachedY = disclaimerActionsY_.load();
  const int y = cachedY >= content.y ? std::min(cachedY, bottomY) : bottomY;
  return Rect{content.x + (content.width - width) / 2, y, width, height};
}

int WeReadActivity::shelfItemsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return shelfGridLayout(renderer, mainContentBounds(), metrics.contentSidePadding, metrics.verticalSpacing)
      .itemsPerPage;
}

void WeReadActivity::resetShelfCoverLoading() {
  if (state_.load() == State::Home) operation_.reset();
  shelfCoverPageStart_ = -1;
  shelfCoverCursor_ = 0;
  shelfCoverStopped_ = false;
}

void WeReadActivity::requestDownloadUpdate() {
  if (!downloadRenderPending_.exchange(true)) requestUpdate();
}

void WeReadActivity::requestJobUpdate() {
  if (retryJob_ == Job::Download) {
    requestDownloadUpdate();
  } else {
    requestUpdate();
  }
}

WeReadActivity::State WeReadActivity::stateForJob(const Job job) {
  switch (job) {
    case Job::Sync:
      return State::Syncing;
    case Job::Detail:
      return State::DetailLoading;
    case Job::Download:
      return State::Downloading;
  }
  return State::Error;
}

bool isPostProcessStage(const WeReadClient::Operation::ProgressStage stage) {
  switch (stage) {
    case WeReadClient::Operation::ProgressStage::Preparing:
    case WeReadClient::Operation::ProgressStage::Packaging:
      return true;
    case WeReadClient::Operation::ProgressStage::Chapters:
    case WeReadClient::Operation::ProgressStage::Images:
      return false;
  }
  return false;
}

void WeReadActivity::connectThen(const Job job, const WeReadStore::ShelfRecord* book) {
  retryJob_ = job;
  if (book) pendingBook_ = *book;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true);
  if (!wifi) {
    LOG_ERR("WR", "OOM: Wi-Fi activity");
    error_ = WeReadClient::Error::OutOfMemory;
    state_.store(State::Error);
    requestJobUpdate();
    return;
  }
  wifiSessionActive_ = true;
  startActivityForResult(std::move(wifi), [this, job](const ActivityResult& result) {
    wifiReleasePending_ = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                          mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavNext);
    if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
      startJob(job, job == Job::Sync ? nullptr : &pendingBook_);
      return;
    }
    error_ = WeReadClient::Error::Network;
    state_.store(State::Error);
    requestJobUpdate();
  });
}

void WeReadActivity::startJob(const Job job, const WeReadStore::ShelfRecord* book) {
  wifiSessionActive_ = true;
  if (job == Job::Sync && shelfFile_.isOpen()) shelfFile_.close();
  if (job != Job::Sync && book) pendingBook_ = *book;
  retryJob_ = job;
  error_ = WeReadClient::Error::Ok;
  progressStage_.store(WeReadClient::Operation::ProgressStage::Chapters);
  progressCompleted_.store(0);
  progressTotal_.store(0);
  postProcessNotice_.store(PostProcessNotice::None);
  postProcessStartedAt_ = 0;
  downloadRenderPending_.store(false);
  stageRenderPending_.store(job == Job::Download);
  qrUrl_[0] = '\0';
  WeReadClient::Operation::Kind kind = WeReadClient::Operation::Kind::Sync;
  switch (job) {
    case Job::Sync:
      kind = WeReadClient::Operation::Kind::Sync;
      break;
    case Job::Detail:
      kind = WeReadClient::Operation::Kind::Detail;
      break;
    case Job::Download:
      kind = WeReadClient::Operation::Kind::Download;
      break;
  }
  WeReadClient::DownloadOptions options;
  if (job == Job::Download && book) {
    options.imagePolicy = detailImagePolicy_;
    options.chapterScope = downloadChapterScope_;
  }
  if (!operation_.begin(kind, book, options, syncShelfCoverScope_)) {
    error_ = operation_.error();
    state_.store(State::Error);
    requestJobUpdate();
  } else {
    const State nextState = job == Job::Detail && detailLoaded_ ? State::DetailCoverLoading : stateForJob(job);
    state_.store(job == Job::Sync ? State::Connecting : nextState);
    if (job == Job::Sync) {
      requestUpdateAndWait();
    } else if (job == Job::Download) {
      requestDownloadUpdate();
    } else if (job == Job::Detail) {
      requestUpdateAndWait();
    }
  }
}

WeReadClient::Operation::Event WeReadActivity::stepOperation() {
  // Rendering owns a second 8KB task and large display buffers. Serialize it
  // with each synchronous protocol step so TLS never competes with a refresh.
  RenderLock renderBarrier(*this);
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  // Cover conversion claims JPEGDEC from the lent 48KB framebuffer.
  std::optional<GfxRenderer::FrameBufferLoan> coverScratch;
  if (operation_.needsCoverConversionScratch()) coverScratch.emplace(renderer);
  struct WorkContext {
    WeReadActivity* activity;
    RenderLock* renderBarrier;
  } context{this, &renderBarrier};
  return operation_.step(
      [](void* rawContext) {
        auto* work = static_cast<WorkContext*>(rawContext);
        work->activity->maybeShowLongWait(*work->renderBarrier);
      },
      &context);
}

void WeReadActivity::updatePostProcessNotice(const WeReadClient::Operation::ProgressStage previous,
                                             const WeReadClient::Operation::ProgressStage current) {
  const bool wasPostProcessing = isPostProcessStage(previous);
  const bool isPostProcessing = isPostProcessStage(current);
  if (wasPostProcessing == isPostProcessing) return;
  if (isPostProcessing) {
    postProcessStartedAt_ = millis();
    postProcessNotice_.store(PostProcessNotice::Waiting);
  } else {
    postProcessStartedAt_ = 0;
    postProcessNotice_.store(PostProcessNotice::None);
  }
}

void WeReadActivity::maybeShowLongWait(RenderLock& renderBarrier) {
  if (postProcessNotice_.load() != PostProcessNotice::Waiting || millis() - postProcessStartedAt_ < kLongWaitMs) {
    return;
  }
  postProcessNotice_.store(PostProcessNotice::LongWait);
  renderBarrier.unlock();
  requestUpdateAndWait();
}

void WeReadActivity::advanceShelfCovers() {
  if (shelfCoverStopped_ || shelfCount_ == 0 || WiFi.status() != WL_CONNECTED) return;
  const int itemsPerPage = shelfItemsPerPage();
  const int pageStart = shelfSelected_.load() / itemsPerPage * itemsPerPage;
  if (pageStart != shelfCoverPageStart_) {
    operation_.reset();
    shelfCoverPageStart_ = pageStart;
    shelfCoverCursor_ = pageStart;
  }

  if (operation_.active()) {
    switch (stepOperation()) {
      case WeReadClient::Operation::Event::None:
      case WeReadClient::Operation::Event::DetailReady:
        return;
      case WeReadClient::Operation::Event::Complete:
        ++shelfCoverCursor_;
        shelfFrameInvalidated_.store(true);
        requestUpdate();
        break;
      case WeReadClient::Operation::Event::QrReady:
      case WeReadClient::Operation::Event::Authenticated:
      case WeReadClient::Operation::Event::ChapterRangeReady:
      case WeReadClient::Operation::Event::ChapterComplete:
      case WeReadClient::Operation::Event::Cancelled:
      case WeReadClient::Operation::Event::Failed:
        operation_.reset();
        shelfCoverStopped_ = true;
        return;
    }
  }

  const int pageEnd = std::min(pageStart + itemsPerPage, static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX)));
  while (shelfCoverCursor_ < pageEnd) {
    WeReadStore::ShelfRecord book;
    if (!readShelf(shelfCoverCursor_, book)) {
      shelfCoverStopped_ = true;
      return;
    }
    const std::string cover = WeReadStore::coverPath(WeReadStore::bookDirectory(book.bookId));
    if (Storage.exists(cover.c_str())) {
      ++shelfCoverCursor_;
      continue;
    }
    if (!operation_.begin(WeReadClient::Operation::Kind::Detail, &book)) {
      operation_.reset();
      shelfCoverStopped_ = true;
    }
    return;
  }
}

void WeReadActivity::updateJobProgress() {
  switch (retryJob_) {
    case Job::Detail:
      return;
    case Job::Sync:
    case Job::Download:
      break;
  }

  const auto stage = operation_.progressStage();
  const uint32_t completed = operation_.progressCompleted();
  const uint32_t total = operation_.progressTotal();
  const auto previousStage = progressStage_.exchange(stage);
  const uint32_t previousCompleted = progressCompleted_.exchange(completed);
  const uint32_t previousTotal = progressTotal_.exchange(total);
  const bool stageChanged = previousStage != stage;
  const bool totalChanged = previousTotal != total;
  const bool completedChanged = previousCompleted != completed;
  const bool decileChanged = WeReadClient::Operation::progressDecile(previousCompleted, total) !=
                             WeReadClient::Operation::progressDecile(completed, total);

  if (retryJob_ == Job::Sync && (stage != WeReadClient::Operation::ProgressStage::Chapters || completed > 0)) {
    state_.store(State::Syncing);
  }
  if (stageChanged && retryJob_ == Job::Download) {
    updatePostProcessNotice(previousStage, stage);
    stageRenderPending_.store(true);
  }

  bool requestRender = stageChanged || totalChanged;
  if (!requestRender && completedChanged) {
    switch (retryJob_) {
      case Job::Sync:
        requestRender = total == 0 ? completed > 0 : decileChanged || completed == total;
        break;
      case Job::Download:
        switch (stage) {
          case WeReadClient::Operation::ProgressStage::Chapters:
            requestRender = true;
            break;
          case WeReadClient::Operation::ProgressStage::Images:
            requestRender = decileChanged || completed == total;
            break;
          case WeReadClient::Operation::ProgressStage::Preparing:
          case WeReadClient::Operation::ProgressStage::Packaging:
            requestRender = completed == total;
            break;
        }
        break;
      case Job::Detail:
        break;
    }
  }
  if (requestRender) requestJobUpdate();
}

void WeReadActivity::advanceJob() {
  const auto event = stepOperation();
  updateJobProgress();

  switch (event) {
    case WeReadClient::Operation::Event::None:
      return;
    case WeReadClient::Operation::Event::QrReady:
      strncpy(qrUrl_, operation_.qrUrl(), sizeof(qrUrl_) - 1);
      qrUrl_[sizeof(qrUrl_) - 1] = '\0';
      state_.store(State::Qr);
      requestJobUpdate();
      return;
    case WeReadClient::Operation::Event::Authenticated:
      state_.store(State::LoginConfirmed);
      return;
    case WeReadClient::Operation::Event::DetailReady:
      if (!detailLoaded_) {
        detailLoadFailed_ = false;
        loadSelectedDetail();
        stageRenderPending_.store(true);
        state_.store(State::DetailCoverLoading);
        requestUpdate();
      }
      return;
    case WeReadClient::Operation::Event::ChapterRangeReady:
      selectChapterRange();
      return;
    case WeReadClient::Operation::Event::ChapterComplete:
      state_.store(State::Downloading);
      return;
    case WeReadClient::Operation::Event::Complete:
      switch (retryJob_) {
        case Job::Sync:
          refreshShelf();
          shelfCoverStopped_ = false;
          mainTab_.store(MainTab::Shelf);
          mainFocus_.store(MainFocus::Content);
          state_.store(State::Home);
          advanceShelfCovers();
          requestJobUpdate();
          return;
        case Job::Detail: {
          const bool preserveUi = state_.load() == State::DetailCoverLoading;
          detailLoadFailed_ = false;
          loadSelectedDetail(preserveUi);
        }
          state_.store(State::Detail);
          requestUpdate();
          return;
        case Job::Download:
          state_.store(State::OpenBook);
          openBook(operation_.finalPath());
          return;
      }
      return;
    case WeReadClient::Operation::Event::Cancelled:
      refreshShelf();
      shelfCoverStopped_ = false;
      state_.store(State::Home);
      requestJobUpdate();
      return;
    case WeReadClient::Operation::Event::Failed:
      if (retryJob_ == Job::Detail) {
        const bool preserveUi = state_.load() == State::DetailCoverLoading;
        detailLoadFailed_ = true;
        loadSelectedDetail(preserveUi);
        state_.store(State::Detail);
        requestUpdate();
        return;
      }
      error_ = operation_.error();
      refreshShelf();
      state_.store(State::Error);
      requestJobUpdate();
      return;
  }
}

void WeReadActivity::loadSelectedDetail(const bool preserveUi) {
  const int previousSelection = detailSelected_;
  const auto previousImagePolicy = detailImagePolicy_;
  detail_ = {};
  introPage_ = 0;
  introPageCount_ = 1;
  introPageOffsets_[0] = 0;
  introPageOffsets_[1] = 0;
  introPagesTruncated_ = false;
  memcpy(detail_.title, pendingBook_.title, sizeof(detail_.title));
  detail_.title[sizeof(detail_.title) - 1] = '\0';
  memcpy(detail_.author, pendingBook_.author, sizeof(detail_.author));
  detail_.author[sizeof(detail_.author) - 1] = '\0';
  detailIntroTruncated_ = false;

  const std::string bookDir = WeReadStore::bookDirectory(pendingBook_.bookId);
  HalFile file;
  WeReadStore::BookDetailHeader cachedDetail;
  detailLoaded_ = WeReadStore::openBookDetail(bookDir, cachedDetail, file);
  if (detailLoaded_) detail_ = cachedDetail;

  WeReadStore::BookOptions options;
  detailOptionsKnown_ = WeReadStore::loadBookOptions(bookDir, options);
  detailSavedImagePolicy_ = options.imagePolicy;
  detailImagePolicy_ = preserveUi ? previousImagePolicy : detailSavedImagePolicy_;
  if (detail_.introLength > 0) buildIntroductionPages();
  const bool cached = Storage.exists(WeReadStore::finalBookPath(pendingBook_).c_str());
  detailSelected_ =
      preserveUi ? previousSelection : static_cast<int>(cached ? DetailAction::Read : DetailAction::Cache);
}

void WeReadActivity::openSelectedDetail(const WeReadStore::ShelfRecord& book) {
  pendingBook_ = book;
  detailLoadFailed_ = false;
  loadSelectedDetail();
  const std::string bookDir = WeReadStore::bookDirectory(book.bookId);
  char coverSource[128];
  int coverSourceLength = snprintf(coverSource, sizeof(coverSource), "%s/cover.source.png", bookDir.c_str());
  bool coverSourceMissing = coverSourceLength <= 0 || static_cast<size_t>(coverSourceLength) >= sizeof(coverSource) ||
                            !Storage.exists(coverSource);
  coverSourceLength = snprintf(coverSource, sizeof(coverSource), "%s/cover.source.jpg", bookDir.c_str());
  coverSourceMissing =
      coverSourceMissing && (coverSourceLength <= 0 || static_cast<size_t>(coverSourceLength) >= sizeof(coverSource) ||
                             !Storage.exists(coverSource));
  const bool coverMissing = detailLoaded_ && detail_.coverUrl[0] &&
                            (!Storage.exists(WeReadStore::coverPath(bookDir).c_str()) || coverSourceMissing);
  if (!detailLoaded_) {
    if (WiFi.status() == WL_CONNECTED) {
      startJob(Job::Detail, &book);
    } else {
      state_.store(State::DetailLoading);
      requestUpdateAndWait();
      connectThen(Job::Detail, &book);
    }
    return;
  }
  if (WiFi.status() == WL_CONNECTED && coverMissing) {
    startJob(Job::Detail, &book);
    return;
  }
  state_.store(State::Detail);
  requestUpdate();
}

void WeReadActivity::activateSelected() {
  WeReadStore::ShelfRecord book;
  if (readShelf(shelfSelected_.load(), book)) openSelectedDetail(book);
}

bool WeReadActivity::detailActionEnabled(const DetailAction action) const {
  switch (action) {
    case DetailAction::Introduction:
      return detailIntroTruncated_;
    case DetailAction::Read:
      return Storage.exists(WeReadStore::finalBookPath(pendingBook_).c_str());
    case DetailAction::Cache:
    case DetailAction::Browse:
    case DetailAction::Images:
      return true;
  }
  return false;
}

void WeReadActivity::moveDetailSelection(const int direction) {
  for (int i = 0; i < kDetailActionCount; ++i) {
    detailSelected_ = direction > 0 ? ButtonNavigator::nextIndex(detailSelected_, kDetailActionCount)
                                    : ButtonNavigator::previousIndex(detailSelected_, kDetailActionCount);
    if (detailActionEnabled(static_cast<DetailAction>(detailSelected_))) return;
  }
}

void WeReadActivity::activateDetailSelection() {
  const auto action = static_cast<DetailAction>(detailSelected_);
  if (!detailActionEnabled(action)) return;
  switch (action) {
    case DetailAction::Introduction:
      buildIntroductionPages();
      introPage_ = 0;
      state_.store(State::Introduction);
      requestUpdate();
      return;
    case DetailAction::Read: {
      const std::string finalPath = WeReadStore::finalBookPath(pendingBook_);
      if (Storage.exists(finalPath.c_str())) {
        openBook(finalPath.c_str());
        return;
      }
      return;
    }
    case DetailAction::Cache:
      showCacheScopePopup();
      return;
    case DetailAction::Browse: {
      // The Activity stack requires heap ownership; the child keeps only a
      // reference to this Activity's fixed Operation workspace.
      auto browse = makeUniqueNoThrow<WeReadBrowseActivity>(renderer, mappedInput, operation_, pendingBook_);
      if (!browse) {
        LOG_ERR("WR", "OOM: browse activity (%zu bytes)", sizeof(WeReadBrowseActivity));
        error_ = WeReadClient::Error::OutOfMemory;
        state_.store(State::Error);
        requestUpdate();
        return;
      }
      startActivityForResult(std::move(browse), [this](const ActivityResult&) {
        if (WiFi.getMode() != WIFI_MODE_NULL) wifiSessionActive_ = true;
      });
      return;
    }
    case DetailAction::Images:
      detailImagePolicy_ = detailImagePolicy_ == WeReadStore::ImagePolicy::Embed ? WeReadStore::ImagePolicy::Exclude
                                                                                 : WeReadStore::ImagePolicy::Embed;
      requestUpdate();
      return;
  }
}

void WeReadActivity::showCacheScopePopup() {
  optionPopup_.show(StrId::STR_WEREAD_CACHE_BOOK, kCacheScopeOptions,
                    static_cast<int>(sizeof(kCacheScopeOptions) / sizeof(kCacheScopeOptions[0])), 0,
                    [this](const int index) {
                      const auto scope = static_cast<WeReadClient::DownloadOptions::ChapterScope>(index);
                      switch (scope) {
                        case WeReadClient::DownloadOptions::ChapterScope::WholeBook:
                        case WeReadClient::DownloadOptions::ChapterScope::SelectRange:
                          downloadChapterScope_ = scope;
                          startBookDownload();
                          return;
                      }
                    });
  requestUpdate();
}

void WeReadActivity::showShelfRefreshPopup() {
  optionPopup_.show(StrId::STR_WEREAD_CACHE_ALL_COVERS_CONFIRM, kShelfRefreshOptions,
                    static_cast<int>(sizeof(kShelfRefreshOptions) / sizeof(kShelfRefreshOptions[0])), 0,
                    [this](const int index) {
                      syncShelf(index == 0 ? WeReadClient::Operation::ShelfCoverScope::None
                                           : WeReadClient::Operation::ShelfCoverScope::All);
                    });
  requestUpdate();
}

void WeReadActivity::startBookDownload() {
  if (WiFi.status() == WL_CONNECTED) {
    startJob(Job::Download, &pendingBook_);
  } else {
    connectThen(Job::Download, &pendingBook_);
  }
}

void WeReadActivity::failChapterRangeSelection(const WeReadClient::Error error) {
  operation_.reset();
  error_ = error;
  state_.store(State::Error);
  requestDownloadUpdate();
}

void WeReadActivity::cancelChapterRangeSelection() {
  operation_.reset();
  downloadChapterScope_ = WeReadClient::DownloadOptions::ChapterScope::WholeBook;
  state_.store(State::Detail);
  requestUpdate();
}

void WeReadActivity::selectChapterRange() {
  const uint32_t chapterCount = operation_.chapterCount();
  if (chapterCount == 0 || chapterCount > static_cast<uint32_t>(INT_MAX)) {
    failChapterRangeSelection(WeReadClient::Error::Protocol);
    return;
  }

  // The Activity stack requires heap ownership. One bounded selector retains a
  // single TocRecord; chapter titles remain in the SD-backed index.
  auto selector =
      makeUniqueNoThrow<WeReadChapterRangeActivity>(renderer, mappedInput, operation_, static_cast<int>(chapterCount));
  if (!selector) {
    LOG_ERR("WR", "OOM: chapter range selector (%zu bytes)", sizeof(WeReadChapterRangeActivity));
    failChapterRangeSelection(WeReadClient::Error::OutOfMemory);
    return;
  }

  startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      cancelChapterRangeSelection();
      return;
    }
    const auto* range = std::get_if<ChapterRangeResult>(&result.data);
    if (!range) {
      failChapterRangeSelection(WeReadClient::Error::SdCard);
      return;
    }
    if (!operation_.setChapterRange(range->first, range->last)) {
      failChapterRangeSelection(WeReadClient::Error::Protocol);
      return;
    }
    progressStage_.store(WeReadClient::Operation::ProgressStage::Chapters);
    progressCompleted_.store(0);
    progressTotal_.store(range->last - range->first + 1);
    stageRenderPending_.store(true);
    state_.store(State::Downloading);
    requestDownloadUpdate();
  });
}

void WeReadActivity::handleDetailInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_.load() == State::DetailCoverLoading) operation_.reset();
    mainTab_.store(MainTab::Shelf);
    mainFocus_.store(MainFocus::Content);
    state_.store(State::Home);
    requestUpdate();
    return;
  }

  const Rect content = contentBounds();
  const Rect actions = detailActionsBounds(content);
  int touchedAction = -1;
  const auto actionTouch = mappedInput.rowTouch(touchedAction, actions.y, GUI.getListRowStep(false),
                                                kDetailListActionCount, actions.x, actions.x + actions.width);
  if (actionTouch != MappedInputManager::RowTouch::None) {
    const auto action = static_cast<DetailAction>(touchedAction + 1);
    if (detailActionEnabled(action)) {
      const int selection = touchedAction + 1;
      if (detailSelected_ != selection) {
        detailSelected_ = selection;
        if (actionTouch == MappedInputManager::RowTouch::Down) requestUpdate();
      }
      if (actionTouch == MappedInputManager::RowTouch::Tap) activateDetailSelection();
    }
    return;
  }

  if (detailIntroTruncated_) {
    const Rect introduction = detailIntroductionBounds(content);
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTouchDown(x, y) && x >= introduction.x && x < introduction.x + introduction.width &&
        y >= introduction.y && y < introduction.y + introduction.height) {
      if (detailSelected_ != static_cast<int>(DetailAction::Introduction)) {
        detailSelected_ = static_cast<int>(DetailAction::Introduction);
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasScreenTapped(x, y) && x >= introduction.x && x < introduction.x + introduction.width &&
        y >= introduction.y && y < introduction.y + introduction.height) {
      detailSelected_ = static_cast<int>(DetailAction::Introduction);
      activateDetailSelection();
      return;
    }
  }

  buttonNavigator_.onNext([this] {
    moveDetailSelection(1);
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this] {
    moveDetailSelection(-1);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateDetailSelection();
  }
}

void WeReadActivity::handleIntroductionInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(retryJob_ == Job::Detail && operation_.active() ? State::DetailCoverLoading : State::Detail);
    requestUpdate();
    return;
  }

  const auto next = [this] {
    if (introPage_ + 1 < introPageCount_) {
      ++introPage_;
      requestUpdate();
    }
  };
  const auto previous = [this] {
    if (introPage_ > 0) {
      --introPage_;
      requestUpdate();
    }
  };
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    next();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    previous();
    return;
  }
  buttonNavigator_.onNext(next);
  buttonNavigator_.onPrevious(previous);
}

void WeReadActivity::buildIntroductionPages() {
  introPage_ = 0;
  introPageCount_ = 1;
  introPagesTruncated_ = false;
  introPageOffsets_[0] = 0;
  introPageOffsets_[1] = detail_.introLength;
  if (!detail_.introLength) return;

  HalFile file;
  WeReadStore::BookDetailHeader header;
  if (!WeReadStore::openBookDetail(WeReadStore::bookDirectory(pendingBook_.bookId), header, file)) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentHeight = renderer.getScreenHeight() - metrics.topPadding - metrics.headerHeight -
                            metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int linesPerPage = std::max(1, (contentHeight - lineHeight - metrics.verticalSpacing) / lineHeight);
  const int maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  uint32_t offset = 0;
  int line = 1;
  int lineWidth = 0;
  while (offset < header.introLength) {
    const uint32_t glyphStart = offset;
    Utf8Glyph glyph;
    if (!readUtf8Glyph(file, header.introLength - offset, glyph)) break;
    offset += glyph.fileBytes;
    if (glyph.textBytes == 1 && glyph.text[0] == '\r') continue;
    if (glyph.textBytes == 1 && glyph.text[0] == '\n') {
      ++line;
      lineWidth = 0;
      if (line > linesPerPage) {
        if (introPageCount_ >= kMaxIntroPages) {
          introPagesTruncated_ = true;
          break;
        }
        introPageOffsets_[introPageCount_++] = offset;
        line = 1;
      }
      continue;
    }

    const int glyphWidth = renderer.getTextAdvanceX(UI_10_FONT_ID, glyph.text, EpdFontFamily::REGULAR);
    if (lineWidth > 0 && lineWidth + glyphWidth > maxWidth) {
      ++line;
      lineWidth = 0;
      if (line > linesPerPage) {
        if (introPageCount_ >= kMaxIntroPages) {
          introPagesTruncated_ = true;
          break;
        }
        introPageOffsets_[introPageCount_++] = glyphStart;
        line = 1;
      }
    }
    lineWidth += glyphWidth;
  }
  introPageOffsets_[introPageCount_] = introPagesTruncated_ ? offset : header.introLength;
}

void WeReadActivity::openBook(const char* path) {
  logHeap("open reader");
#ifdef CROSSPOINT_EMULATED
  activityManager.goToReader(path);
#else
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    activityManager.goToReader(path);
    return;
  }

  APP_STATE.openEpubPath = path;
  APP_STATE.readerActivityLoadCount = 0;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("WR", "Failed to persist reader target; opening without restart");
    activityManager.goToReader(path);
    return;
  }

  WiFi.disconnect(false);
  delay(30);
  silentRestartToReader();
#endif
}

void WeReadActivity::openShelf() {
  if (refreshShelf()) {
    state_.store(State::Home);
    requestUpdate();
    return;
  }
  syncShelf();
}

void WeReadActivity::syncShelf(const WeReadClient::Operation::ShelfCoverScope scope) {
  syncShelfCoverScope_ = scope;
  if (WiFi.status() == WL_CONNECTED) {
    startJob(Job::Sync);
  } else {
    connectThen(Job::Sync);
  }
}

void WeReadActivity::activateDisclaimerSelection() {
  if (disclaimerSelected_ == 0) {
    activityManager.goToApps();
    return;
  }
  if (!WeReadStore::acceptDisclaimer()) {
    LOG_ERR("WR", "Failed to persist disclaimer acceptance");
    disclaimerSaveFailed_ = true;
    requestUpdate();
    return;
  }
  enterApp();
}

void WeReadActivity::handleDisclaimerInput() {
  const Rect actions = disclaimerActionsBounds();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gap = disclaimerActionGap(actions.width, metrics.verticalSpacing);
  const int buttonWidth = std::max(1, (actions.width - gap) / kDisclaimerActionCount);
  int touched = -1;
  switch (mappedInput.colTouch(touched, actions.x, buttonWidth + gap, kDisclaimerActionCount, actions.y,
                               actions.y + actions.height, buttonWidth)) {
    case MappedInputManager::RowTouch::Tap:
      disclaimerSelected_ = touched;
      activateDisclaimerSelection();
      return;
    case MappedInputManager::RowTouch::Down:
      if (disclaimerSelected_ != touched) {
        disclaimerSelected_ = touched;
        requestUpdate();
      }
      return;
    case MappedInputManager::RowTouch::None:
      break;
  }

  buttonNavigator_.onNextRelease([this] {
    disclaimerSelected_ = ButtonNavigator::nextIndex(disclaimerSelected_, kDisclaimerActionCount);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this] {
    disclaimerSelected_ = ButtonNavigator::previousIndex(disclaimerSelected_, kDisclaimerActionCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateDisclaimerSelection();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void WeReadActivity::promptLogout() {
  // ActivityManager owns the confirmation across frames, so this must be a
  // fallible heap allocation rather than a stack object.
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_WEREAD_LOGOUT_CONFIRM),
                                                              tr(STR_WEREAD_LOGOUT_KEEP_DOWNLOADS));
  if (!confirmation) {
    LOG_ERR("WR", "OOM: logout confirmation (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    performLogout();
  });
}

void WeReadActivity::promptClearCache() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_WEREAD_MENU_CLEAR_CACHE),
                                                              tr(STR_WEREAD_CLEAR_CACHE_KEEP_BOOKS));
  if (!confirmation) {
    LOG_ERR("WR", "OOM: clear cache confirmation (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    performClearCache();
  });
}

void WeReadActivity::performClearCache() {
  operation_.reset();
  if (shelfFile_.isOpen()) shelfFile_.close();
  state_.store(State::ClearingCache);
  requestUpdateAndWait();

  const bool cleared = WeReadStore::clearCache();
  refreshShelf();
  resetShelfCoverLoading();
  detail_ = {};
  detailLoaded_ = false;
  detailLoadFailed_ = false;
  detailOptionsKnown_ = false;
  detailIntroTruncated_ = false;
  introPage_ = 0;
  introPageCount_ = 1;
  state_.store(cleared ? State::CacheCleared : State::CacheClearError);
  requestUpdate();
}

void WeReadActivity::performLogout() {
  operation_.reset();
  if (shelfFile_.isOpen()) shelfFile_.close();
  const bool sessionCleared = WeReadStore::clearSession();
  const bool shelfCleared = WeReadStore::clearShelf();
  const bool browseCacheCleared = WeReadBrowse::clearAllCaches();
  shelfCount_ = 0;
  shelfSelected_.store(0);
  shelfFrameInvalidated_.store(true);
  if (!sessionCleared || !shelfCleared || !browseCacheCleared) {
    LOG_ERR("WR", "Failed to clear local login state");
    state_.store(State::LogoutError);
    requestUpdate();
    return;
  }
  mainTab_.store(MainTab::Shelf);
  mainFocus_.store(MainFocus::Content);
  syncShelf();
}

void WeReadActivity::selectMainTab(const MainTab tab) {
  if (mainTab_.load() == tab) return;
  resetShelfCoverLoading();
  mainTab_.store(tab);
  requestUpdate();
}

void WeReadActivity::handleMainTabInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    mainFocus_.store(MainFocus::Content);
    requestUpdate();
    return;
  }

  const bool swapFrontDirections = mappedInput.isNavDirectionSwapped();
  const auto previousButton =
      swapFrontDirections ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFrontDirections ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  if (!mappedInput.wasReleased(previousButton) && !mappedInput.wasReleased(nextButton)) return;

  selectMainTab(mainTab_.load() == MainTab::Shelf ? MainTab::Manage : MainTab::Shelf);
}

void WeReadActivity::handleManageInput() {
  const auto activate = [this] {
    switch (kManageEntries[manageSelected_].action) {
      case ManageAction::Refresh:
        showShelfRefreshPopup();
        return;
      case ManageAction::ClearCache:
        promptClearCache();
        return;
      case ManageAction::Logout:
        promptLogout();
        return;
    }
  };

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = mainContentBounds();
  int touched = -1;
  const auto touch = mappedInput.rowTouch(touched, content.y, metrics.menuRowHeight + metrics.menuSpacing,
                                          kManageEntryCount, content.x, content.x + content.width);
  if (touch != MappedInputManager::RowTouch::None) {
    const bool changed = manageSelected_ != touched || mainFocus_.load() != MainFocus::Content;
    manageSelected_ = touched;
    mainFocus_.store(MainFocus::Content);
    if (touch == MappedInputManager::RowTouch::Tap) {
      activate();
    } else if (changed) {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (manageSelected_ == 0) {
      mainFocus_.store(MainFocus::Tabs);
    } else {
      --manageSelected_;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    manageSelected_ = ButtonNavigator::nextIndex(manageSelected_, kManageEntryCount);
    requestUpdate();
    return;
  }

  const bool swapFrontDirections = mappedInput.isNavDirectionSwapped();
  const auto previousButton =
      swapFrontDirections ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFrontDirections ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  if (mappedInput.wasReleased(previousButton)) {
    if (manageSelected_ == 0) {
      mainFocus_.store(MainFocus::Tabs);
    } else {
      --manageSelected_;
    }
    requestUpdate();
  } else if (mappedInput.wasReleased(nextButton)) {
    manageSelected_ = ButtonNavigator::nextIndex(manageSelected_, kManageEntryCount);
    requestUpdate();
  }
}

void WeReadActivity::handleMainInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    resetShelfCoverLoading();
    activityManager.goToApps();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect tabs{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, metrics.tabBarHeight};
  int x = 0;
  int y = 0;
  int touchedTab = -1;
  if ((mappedInput.wasScreenTouchDown(x, y) || mappedInput.wasScreenTapped(x, y)) &&
      GUI.tabIndexFromPoint(renderer, tabs, mainTabs_, x, y, touchedTab)) {
    mainFocus_.store(MainFocus::Content);
    selectMainTab(touchedTab == 0 ? MainTab::Shelf : MainTab::Manage);
    requestUpdate();
    return;
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Left:
      mainFocus_.store(MainFocus::Content);
      selectMainTab(MainTab::Manage);
      requestUpdate();
      return;
    case MappedInputManager::SwipeDir::Right:
      mainFocus_.store(MainFocus::Content);
      selectMainTab(MainTab::Shelf);
      requestUpdate();
      return;
    case MappedInputManager::SwipeDir::Up:
    case MappedInputManager::SwipeDir::Down:
      if (mainTab_.load() == MainTab::Shelf) mainFocus_.store(MainFocus::Content);
      break;
    case MappedInputManager::SwipeDir::None:
      break;
  }

  if (mainFocus_.load() == MainFocus::Tabs) {
    handleMainTabInput();
    return;
  }

  switch (mainTab_.load()) {
    case MainTab::Shelf:
      handleShelfInput();
      if (state_.load() == State::Home && mainTab_.load() == MainTab::Shelf) advanceShelfCovers();
      return;
    case MainTab::Manage:
      handleManageInput();
      return;
  }
}

void WeReadActivity::handleShelfInput() {
  const int count = static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout =
      shelfGridLayout(renderer, mainContentBounds(), metrics.contentSidePadding, metrics.verticalSpacing);
  const int itemsPerPage = layout.itemsPerPage;
  const Rect content = mainContentBounds();

  switch (shelfNavigationGesture_) {
    case ShelfNavigationGesture::Idle:
      if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
        shelfNavigationGesture_ = ShelfNavigationGesture::PreviousPressed;
      } else if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
        shelfNavigationGesture_ = ShelfNavigationGesture::NextPressed;
      } else {
        break;
      }
      [[fallthrough]];
    case ShelfNavigationGesture::PreviousPressed:
    case ShelfNavigationGesture::NextPressed:
    case ShelfNavigationGesture::PreviousPageHandled:
    case ShelfNavigationGesture::NextPageHandled: {
      const bool previous = shelfNavigationGesture_ == ShelfNavigationGesture::PreviousPressed ||
                            shelfNavigationGesture_ == ShelfNavigationGesture::PreviousPageHandled;
      const bool pageHandled = shelfNavigationGesture_ == ShelfNavigationGesture::PreviousPageHandled ||
                               shelfNavigationGesture_ == ShelfNavigationGesture::NextPageHandled;
      const auto button = previous ? MappedInputManager::Button::NavPrevious : MappedInputManager::Button::NavNext;
      if (mappedInput.wasReleased(button)) {
        shelfNavigationGesture_ = ShelfNavigationGesture::Idle;
        shelfLastPageTurnAt_ = 0;
        if (pageHandled) return;

        const int selected = shelfSelected_.load();
        const int target = previous ? previousShelfIndexOrTab(selected) : ButtonNavigator::nextIndex(selected, count);
        if (target == kNoShelfSelection) {
          mainFocus_.store(MainFocus::Tabs);
          requestUpdate();
          return;
        }
        moveShelfSelection(target, itemsPerPage);
        return;
      }

      if (!mappedInput.isPressed(button)) {
        shelfNavigationGesture_ = ShelfNavigationGesture::Idle;
        shelfLastPageTurnAt_ = 0;
        return;
      }

      const uint32_t now = millis();
      if ((!pageHandled && mappedInput.getHeldTime() < kShelfPageHoldMs) ||
          (pageHandled && now - shelfLastPageTurnAt_ < kShelfPageHoldMs)) {
        return;
      }

      shelfNavigationGesture_ =
          previous ? ShelfNavigationGesture::PreviousPageHandled : ShelfNavigationGesture::NextPageHandled;
      shelfLastPageTurnAt_ = now;
      if (count > itemsPerPage) {
        const int selected = shelfSelected_.load();
        const int target = previous ? ButtonNavigator::previousPageIndex(selected, count, itemsPerPage)
                                    : ButtonNavigator::nextPageIndex(selected, count, itemsPerPage);
        moveShelfSelection(target, itemsPerPage);
      }
      return;
    }
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = weReadShelfIndexFromPoint(content, layout, shelfSelected_.load(), count, x, y);
    if (touched >= 0) {
      moveShelfSelection(touched, itemsPerPage);
      return;
    }
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = weReadShelfIndexFromPoint(content, layout, shelfSelected_.load(), count, x, y);
    if (touched >= 0) {
      moveShelfSelection(touched, itemsPerPage);
      resetShelfCoverLoading();
      activateSelected();
      return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (count > 0) {
    switch (swipe) {
      case MappedInputManager::SwipeDir::Up:
        moveShelfSelection(ButtonNavigator::nextPageIndex(shelfSelected_.load(), count, itemsPerPage), itemsPerPage);
        return;
      case MappedInputManager::SwipeDir::Down:
        moveShelfSelection(ButtonNavigator::previousPageIndex(shelfSelected_.load(), count, itemsPerPage),
                           itemsPerPage);
        return;
      case MappedInputManager::SwipeDir::Left:
      case MappedInputManager::SwipeDir::Right:
      case MappedInputManager::SwipeDir::None:
        break;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    resetShelfCoverLoading();
    activateSelected();
  }
}

void WeReadActivity::moveShelfSelection(const int index, const int itemsPerPage) {
  const int previousIndex = shelfSelected_.exchange(index);
  if (index == previousIndex) return;
  if (index / itemsPerPage != previousIndex / itemsPerPage) resetShelfCoverLoading();
  requestUpdate();
}

void WeReadActivity::handleErrorInput() {
  int x = 0;
  int y = 0;
  const bool confirm =
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y);
  if (error_ == WeReadClient::Error::WholeBookOnly) {
    if (confirm) {
      operation_.reset();
      state_.store(State::Detail);
      showCacheScopePopup();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      operation_.reset();
      state_.store(State::Detail);
      requestUpdate();
    }
    return;
  }

  if (confirm) {
    if (WiFi.status() == WL_CONNECTED) {
      switch (retryJob_) {
        case Job::Sync:
          startJob(Job::Sync);
          break;
        case Job::Detail:
        case Job::Download:
          startJob(retryJob_, &pendingBook_);
          break;
      }
    } else {
      switch (retryJob_) {
        case Job::Sync:
          connectThen(Job::Sync);
          break;
        case Job::Detail:
        case Job::Download:
          connectThen(retryJob_, &pendingBook_);
          break;
      }
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    operation_.reset();
    if (retryJob_ != Job::Sync) {
      mainTab_.store(MainTab::Shelf);
      mainFocus_.store(MainFocus::Content);
    }
    state_.store(State::Home);
    requestJobUpdate();
  }
}

void WeReadActivity::handleLogoutErrorInput() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    performLogout();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(State::Home);
    requestUpdate();
  }
}

void WeReadActivity::loop() {
  if (wifiReleasePending_) {
    const bool held = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                      mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                      mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                      mappedInput.isPressed(MappedInputManager::Button::NavNext);
    if (!held) wifiReleasePending_ = false;
    return;
  }

  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) {
    optionPopupClosing_ = !optionPopup_.isActive();
    return;
  }
  if (optionPopupClosing_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;
    }
    optionPopupClosing_ = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;
    }
  }

  const State state = state_.load();
  switch (state) {
    case State::Disclaimer:
      handleDisclaimerInput();
      return;
    case State::Home:
      handleMainInput();
      return;
    case State::Detail:
      handleDetailInput();
      return;
    case State::DetailCoverLoading:
      if (stageRenderPending_.load()) return;
      handleDetailInput();
      if (state_.load() != State::DetailCoverLoading) return;
      advanceJob();
      return;
    case State::Introduction:
      handleIntroductionInput();
      return;
    case State::Error:
      handleErrorInput();
      return;
    case State::LogoutError:
      handleLogoutErrorInput();
      return;
    case State::CacheCleared: {
      int x = 0;
      int y = 0;
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
          mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
        state_.store(State::Home);
        requestUpdate();
      }
      return;
    }
    case State::CacheClearError: {
      int x = 0;
      int y = 0;
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
        performClearCache();
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        state_.store(State::Home);
        requestUpdate();
      }
      return;
    }
    case State::ClearingCache:
      return;
    case State::LoginConfirmed:
      requestUpdateAndWait();
      state_.store(retryJob_ == Job::Detail && detailLoaded_ ? State::DetailCoverLoading : stateForJob(retryJob_));
      return;
    case State::OpenBook:
      return;
    case State::Connecting:
    case State::Qr:
    case State::Syncing:
    case State::DetailLoading:
    case State::Downloading:
    case State::Cancelling:
      break;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    operation_.cancel();
    state_.store(State::Cancelling);
    requestJobUpdate();
    return;
  }
  if (state == State::Downloading && stageRenderPending_.load()) {
    return;
  }
  advanceJob();
}

bool WeReadActivity::isBusy(const State state) {
  return state == State::Connecting || state == State::Qr || state == State::LoginConfirmed ||
         state == State::Syncing || state == State::DetailLoading || state == State::DetailCoverLoading ||
         state == State::Downloading || state == State::Cancelling || state == State::ClearingCache;
}

const char* WeReadActivity::errorMessage() const {
  switch (error_) {
    case WeReadClient::Error::SdCard:
      return tr(STR_WEREAD_CACHE_FAILED);
    case WeReadClient::Error::Network:
      return WiFi.status() == WL_CONNECTED ? tr(STR_WEREAD_HTTP_ERROR) : tr(STR_WEREAD_NO_WIFI);
    case WeReadClient::Error::Unavailable:
      return tr(STR_WEREAD_CACHE_NOT_AVAILABLE);
    case WeReadClient::Error::WholeBookOnly:
      return tr(STR_WEREAD_CACHE_WHOLE_BOOK_ONLY);
    default:
      return tr(STR_WEREAD_HTTP_ERROR);
  }
}

bool WeReadActivity::drawDetailIntroduction(const Rect& bounds, const bool selected) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textX = bounds.x;
  const int textWidth = std::max(1, bounds.width);
  const int titleY = bounds.y;
  const int textY = titleY + lineHeight + 4;
  const int maxLines = std::max(0, (bounds.y + bounds.height - textY) / lineHeight);
  const bool black = !selected;

  if (selected) renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height);
  renderer.drawText(UI_10_FONT_ID, textX, titleY, tr(STR_WEREAD_INTRO), black, EpdFontFamily::BOLD);

  if (maxLines == 0) return detail_.introLength > 0;
  if (!detail_.introLength) {
    renderer.drawText(UI_10_FONT_ID, textX, textY,
                      detailLoadFailed_ && !detailLoaded_ ? tr(STR_WEREAD_DETAIL_UNAVAILABLE) : tr(STR_WEREAD_NO_INTRO),
                      black);
    return false;
  }

  HalFile file;
  WeReadStore::BookDetailHeader header;
  if (!WeReadStore::openBookDetail(WeReadStore::bookDirectory(pendingBook_.bookId), header, file) ||
      !file.seek(WeReadStore::kBookDetailHeaderSize)) {
    renderer.drawText(UI_10_FONT_ID, textX, textY, tr(STR_WEREAD_DETAIL_UNAVAILABLE), black);
    return false;
  }

  uint32_t offset = 0;
  int y = textY;
  for (int lineIndex = 0; lineIndex < maxLines && offset < header.introLength; ++lineIndex) {
    char line[192] = {};
    size_t lineLength = 0;
    int lineWidth = 0;

    while (offset < header.introLength) {
      const uint32_t glyphStart = offset;
      Utf8Glyph glyph;
      if (!readUtf8Glyph(file, header.introLength - offset, glyph)) return false;
      offset += glyph.fileBytes;
      if (glyph.textBytes == 1 && glyph.text[0] == '\r') continue;
      if (glyph.textBytes == 1 && glyph.text[0] == '\n') break;

      const int glyphWidth = renderer.getTextAdvanceX(UI_10_FONT_ID, glyph.text, EpdFontFamily::REGULAR);
      if ((lineWidth > 0 && lineWidth + glyphWidth > textWidth) || lineLength + glyph.textBytes >= sizeof(line)) {
        offset = glyphStart;
        if (!file.seek(WeReadStore::kBookDetailHeaderSize + offset)) return false;
        break;
      }
      memcpy(line + lineLength, glyph.text, glyph.textBytes);
      lineLength += glyph.textBytes;
      lineWidth += glyphWidth;
    }

    const bool truncated = lineIndex + 1 == maxLines && offset < header.introLength;
    if (truncated) {
      static constexpr char kEllipsis[] = "...";
      const int ellipsisWidth = renderer.getTextAdvanceX(UI_10_FONT_ID, kEllipsis, EpdFontFamily::REGULAR);
      while (lineLength > 0 &&
             (lineWidth + ellipsisWidth > textWidth || lineLength + sizeof(kEllipsis) > sizeof(line))) {
        size_t glyphStart = lineLength - 1;
        while (glyphStart > 0 && (static_cast<uint8_t>(line[glyphStart]) & 0xC0) == 0x80) --glyphStart;
        char removed[5] = {};
        const size_t removedLength = lineLength - glyphStart;
        memcpy(removed, line + glyphStart, removedLength);
        lineWidth -= renderer.getTextAdvanceX(UI_10_FONT_ID, removed, EpdFontFamily::REGULAR);
        lineLength = glyphStart;
      }
      memcpy(line + lineLength, kEllipsis, sizeof(kEllipsis));
      lineLength += sizeof(kEllipsis) - 1;
    } else {
      line[lineLength] = '\0';
    }

    if (lineLength > 0) renderer.drawText(UI_10_FONT_ID, textX, y, line, black);
    y += lineHeight;
    if (truncated) return true;
  }
  return false;
}

void WeReadActivity::drawDisclaimer(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect actions = disclaimerActionsBounds();
  const int paragraphSpacing = metrics.verticalSpacing;
  const int textWidth = std::max(0, content.width - metrics.contentSidePadding * 2);
  freeink::ui::GfxRendererTarget target(renderer);
  target.setFont(freeink::ui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  freeink::ui::TextStyle textStyle;
  textStyle.font = freeink::ui::GfxRendererTarget::FONT_BODY;
  textStyle.maxLines = 16;
  int paragraphHeights[kDisclaimerParagraphCount] = {};
  int textHeight = paragraphSpacing * (kDisclaimerParagraphCount - 1);
  for (int i = 0; i < kDisclaimerParagraphCount; ++i) {
    paragraphHeights[i] =
        freeink::ui::measureWrappedText(target, I18N.get(kDisclaimerParagraphs[i]), textStyle, textWidth).height;
    textHeight += paragraphHeights[i];
  }
  const int freeHeight = std::max(0, content.height - textHeight - actions.height);
  const int actionGap = std::min(freeHeight, std::max(metrics.verticalSpacing, freeHeight / 3));
  actions.y = std::min(content.y + content.height - actions.height, content.y + textHeight + actionGap);
  disclaimerActionsY_.store(actions.y);
  const Rect textBounds{
      content.x + metrics.contentSidePadding,
      content.y,
      textWidth,
      std::max(0, actions.y - actionGap - content.y),
  };
  int y = textBounds.y;
  {
    GfxRenderer::ClipScope clip(renderer, textBounds.x, textBounds.y, textBounds.width, textBounds.height);
    for (int i = 0; i < kDisclaimerParagraphCount; ++i) {
      const char* paragraph = I18N.get(kDisclaimerParagraphs[i]);
      const freeink::ui::Rect paragraphBounds{
          static_cast<int16_t>(textBounds.x),
          static_cast<int16_t>(y),
          static_cast<int16_t>(textBounds.width),
          static_cast<int16_t>(paragraphHeights[i]),
      };
      freeink::ui::layoutText(target, paragraphBounds, paragraph, textStyle,
                              [&](const char* line, const freeink::ui::Rect lineBounds) {
                                renderer.drawText(UI_10_FONT_ID, lineBounds.x, lineBounds.y, line);
                              });
      y += paragraphHeights[i];
      if (i + 1 < kDisclaimerParagraphCount) y += paragraphSpacing;
    }
  }

  const int buttonGap = disclaimerActionGap(actions.width, metrics.verticalSpacing);
  const int buttonWidth = std::max(1, (actions.width - buttonGap) / kDisclaimerActionCount);
  const int buttonRadius = std::min(metrics.popupCornerRadius, actions.height / 2);
  const int buttonLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (int i = 0; i < kDisclaimerActionCount; ++i) {
    const int buttonX = actions.x + i * (buttonWidth + buttonGap);
    const bool selected = disclaimerSelected_ == i;
    renderer.fillRoundedRect(buttonX, actions.y, buttonWidth, actions.height, buttonRadius,
                             selected ? Color::Black : Color::White);
    renderer.drawRoundedRect(buttonX, actions.y, buttonWidth, actions.height, 1, buttonRadius, true);
    const char* label = I18N.get(kDisclaimerActions[i]);
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    renderer.drawText(UI_10_FONT_ID, buttonX + (buttonWidth - labelWidth) / 2,
                      actions.y + (actions.height - buttonLineHeight) / 2, label, !selected);
  }

  if (disclaimerSaveFailed_) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_DISCLAIMER_SAVE_FAILED));
  }
}

void WeReadActivity::drawShelfGrid(const Rect& content, const int selectedIndex, const int frameSelection,
                                   const bool contentFocused) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = shelfGridLayout(renderer, content, metrics.contentSidePadding, metrics.verticalSpacing);
  const int count = static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX));
  const int page = selectedIndex / layout.itemsPerPage;
  const int pageStart = page * layout.itemsPerPage;
  const int pageEnd = std::min(pageStart + layout.itemsPerPage, count);
  const bool incrementalFrame = frameSelection >= pageStart && frameSelection < pageEnd;

  const auto drawItem = [&](const int index, const bool selected) {
    const auto geometry = weReadShelfItemGeometry(content, layout, pageStart, pageEnd, index);
    const Rect cover = geometry.cover;
    const Rect itemBounds = geometry.hit;
    const bool focused = selected && contentFocused;
    bool foregroundBlack = true;
    if (focused) {
      foregroundBlack = GUI.drawSelectionBackground(renderer, itemBounds);
      renderer.fillRect(cover.x, cover.y, cover.width, cover.height, false);
    } else if (incrementalFrame) {
      renderer.fillRect(itemBounds.x, itemBounds.y, itemBounds.width, itemBounds.height, false);
    }

    WeReadStore::ShelfRecord book;
    if (!readShelf(index, book)) return;

    const bool coverDrawn = drawCachedCover(renderer, WeReadStore::bookDirectory(book.bookId), cover);
    renderer.drawRect(cover.x, cover.y, cover.width, cover.height);
    if (!coverDrawn) {
      renderer.drawIcon(CoverIcon, cover.x + (cover.width - 32) / 2, cover.y + (cover.height - 32) / 2, 32);
    }

    const std::string title = renderer.truncatedText(SMALL_FONT_ID, book.title, layout.coverWidth);
    const int titleWidth = renderer.getTextAdvanceX(SMALL_FONT_ID, title.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, cover.x + std::max(0, (cover.width - titleWidth) / 2),
                      cover.y + cover.height + layout.titleGap, title.c_str(), foregroundBlack);
  };

  if (incrementalFrame) {
    drawItem(frameSelection, false);
    drawItem(selectedIndex, true);
  } else {
    for (int index = pageStart; index < pageEnd; ++index) drawItem(index, index == selectedIndex);
  }

  GUI.drawSideScrollBar(renderer, content, count, pageStart, layout.itemsPerPage);
}

void WeReadActivity::drawBookDetail(const Rect& content, const bool coverLoading) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const Rect cover{content.x + side, content.y, kDetailCoverWidth, kDetailCoverHeight};
  renderer.drawRect(cover.x, cover.y, cover.width, cover.height);
  const bool coverDrawn = drawCachedCover(renderer, WeReadStore::bookDirectory(pendingBook_.bookId),
                                          Rect{cover.x + 2, cover.y + 2, cover.width - 4, cover.height - 4});
  if (!coverDrawn) {
    UITheme::drawCenteredWrappedText(
        renderer, cover, UI_10_FONT_ID,
        I18N.get(coverLoading ? StrId::STR_WEREAD_COVER_LOADING : StrId::STR_WEREAD_NO_COVER), 2);
  }

  const int metaX = cover.x + cover.width + 16;
  const int metaWidth = std::max(1, content.x + content.width - metaX - side);
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int detailLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int cacheY = cover.y + cover.height - smallLineHeight;
  const int requiredDetailHeight =
      (detail_.author[0] ? detailLineHeight : 0) + (detail_.newRating > 0 ? detailLineHeight : 0);
  const int maxTitleLines = std::clamp((cacheY - cover.y - requiredDetailHeight) / titleLineHeight, 1, 2);
  int metaY = cover.y;
  const auto titleLines =
      renderer.wrappedText(UI_12_FONT_ID, detail_.title, metaWidth, maxTitleLines, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, metaX, metaY, line.c_str(), true, EpdFontFamily::BOLD);
    metaY += titleLineHeight;
  }
  if (detail_.author[0]) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, detail_.author, metaWidth);
    renderer.drawText(UI_10_FONT_ID, metaX, metaY, author.c_str());
    metaY += detailLineHeight;
  }
  if (detail_.newRating > 0) {
    char rating[48];
    snprintf(rating, sizeof(rating), tr(STR_WEREAD_RATING_FMT), detail_.newRating / 100.0);
    renderer.drawText(UI_10_FONT_ID, metaX, metaY, rating);
    metaY += detailLineHeight;
  }

  const bool cached = Storage.exists(WeReadStore::finalBookPath(pendingBook_).c_str());
  const bool policyChanged = cached && detailOptionsKnown_ && detailImagePolicy_ != detailSavedImagePolicy_;
  const char* cacheState = !cached ? tr(STR_WEREAD_NOT_CACHED)
                                   : (policyChanged ? tr(STR_WEREAD_CACHE_NEEDS_UPDATE) : tr(STR_WEREAD_CACHE_BADGE));
  renderer.drawText(SMALL_FONT_ID, metaX, cacheY, cacheState, true, EpdFontFamily::BOLD);

  char minor[192] = {};
  if (detail_.category[0] && detail_.totalWords > 0) {
    char words[48];
    snprintf(words, sizeof(words), tr(STR_WEREAD_WORDS_FMT), static_cast<unsigned>(detail_.totalWords));
    snprintf(minor, sizeof(minor), "%s · %s", detail_.category, words);
  } else if (detail_.category[0]) {
    snprintf(minor, sizeof(minor), "%s", detail_.category);
  } else if (detail_.totalWords > 0) {
    snprintf(minor, sizeof(minor), tr(STR_WEREAD_WORDS_FMT), static_cast<unsigned>(detail_.totalWords));
  } else if (detail_.publisher[0]) {
    snprintf(minor, sizeof(minor), "%s", detail_.publisher);
  }
  const int minorY = cacheY - smallLineHeight;
  if (minor[0] && metaY <= minorY) {
    const auto text = renderer.truncatedText(SMALL_FONT_ID, minor, metaWidth);
    renderer.drawText(SMALL_FONT_ID, metaX, minorY, text.c_str());
  }

  const Rect actions = detailActionsBounds(content);
  const Rect introduction = detailIntroductionBounds(content);
  detailIntroTruncated_ =
      drawDetailIntroduction(introduction, detailSelected_ == static_cast<int>(DetailAction::Introduction));

  GUI.drawList(
      renderer, actions, kDetailListActionCount,
      detailSelected_ == static_cast<int>(DetailAction::Introduction) ? -1 : detailSelected_ - 1,
      [cached, policyChanged](const int index) {
        switch (static_cast<DetailAction>(index + 1)) {
          case DetailAction::Introduction:
            return std::string();
          case DetailAction::Read:
            return std::string(I18N.get(cached ? StrId::STR_CONTINUE_READING : StrId::STR_WEREAD_ONLINE_READING));
          case DetailAction::Cache:
            if (!cached) return std::string(tr(STR_WEREAD_CACHE_BOOK));
            return std::string(
                I18N.get(policyChanged ? StrId::STR_WEREAD_UPDATE_CACHE : StrId::STR_WEREAD_RECACHE_BOOK));
          case DetailAction::Browse:
            return std::string(tr(STR_WEREAD_BROWSE_ENTRY));
          case DetailAction::Images:
            return std::string(tr(STR_WEREAD_CACHE_IMAGES));
        }
        return std::string();
      },
      nullptr, nullptr,
      [this, cached](const int index) {
        switch (static_cast<DetailAction>(index + 1)) {
          case DetailAction::Introduction:
          case DetailAction::Browse:
          case DetailAction::Cache:
            return std::string();
          case DetailAction::Read:
            return cached ? std::string() : std::string(tr(STR_WEREAD_FUTURE_SUPPORT));
          case DetailAction::Images:
            return std::string(I18N.get(detailImagePolicy_ == WeReadStore::ImagePolicy::Embed
                                            ? StrId::STR_WEREAD_OPTION_ON
                                            : StrId::STR_WEREAD_OPTION_OFF));
        }
        return std::string();
      },
      false,
      [cached](const int index) { return static_cast<DetailAction>(index + 1) == DetailAction::Read && !cached; });
}

void WeReadActivity::drawIntroduction(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxWidth = content.width - side * 2;
  const int footerY = content.y + content.height - lineHeight;
  const uint32_t start = introPageOffsets_[introPage_];
  const uint32_t end = introPageOffsets_[introPage_ + 1];

  HalFile file;
  WeReadStore::BookDetailHeader header;
  if (!WeReadStore::openBookDetail(WeReadStore::bookDirectory(pendingBook_.bookId), header, file) ||
      !file.seek(WeReadStore::kBookDetailHeaderSize + start)) {
    renderer.drawText(UI_10_FONT_ID, content.x + side, content.y, tr(STR_WEREAD_DETAIL_UNAVAILABLE));
    return;
  }

  char line[192] = {};
  size_t lineLength = 0;
  int lineWidth = 0;
  int y = content.y;
  uint32_t offset = start;
  const auto flushLine = [&]() {
    line[lineLength] = '\0';
    if (lineLength > 0) renderer.drawText(UI_10_FONT_ID, content.x + side, y, line);
    lineLength = 0;
    lineWidth = 0;
    y += lineHeight;
  };

  while (offset < end && y < footerY) {
    Utf8Glyph glyph;
    if (!readUtf8Glyph(file, end - offset, glyph)) break;
    offset += glyph.fileBytes;
    if (glyph.textBytes == 1 && glyph.text[0] == '\r') continue;
    if (glyph.textBytes == 1 && glyph.text[0] == '\n') {
      flushLine();
      continue;
    }
    const int glyphWidth = renderer.getTextAdvanceX(UI_10_FONT_ID, glyph.text, EpdFontFamily::REGULAR);
    if ((lineWidth > 0 && lineWidth + glyphWidth > maxWidth) || lineLength + glyph.textBytes >= sizeof(line)) {
      flushLine();
      if (y >= footerY) break;
    }
    memcpy(line + lineLength, glyph.text, glyph.textBytes);
    lineLength += glyph.textBytes;
    lineWidth += glyphWidth;
  }
  if (lineLength > 0 && y < footerY) flushLine();
  if (introPagesTruncated_ && introPage_ + 1 == introPageCount_ && y < footerY) {
    renderer.drawText(UI_10_FONT_ID, content.x + side, y, "...");
  }
  char page[32];
  snprintf(page, sizeof(page), tr(STR_WEREAD_PAGE_FMT), static_cast<unsigned>(introPage_ + 1),
           static_cast<unsigned>(introPageCount_));
  renderer.drawCenteredText(SMALL_FONT_ID, footerY, page);
}

void WeReadActivity::render(RenderLock&&) {
  downloadRenderPending_.store(false);
  stageRenderPending_.store(false);
  if (optionPopup_.processRender(renderer, mappedInput)) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const State state = state_.load();
  const MainTab mainTab = mainTab_.load();
  const MainFocus mainFocus = mainFocus_.load();
  const int shelfSelection = shelfSelected_.load();
  const Rect content = state == State::Disclaimer ? disclaimerContentBounds()
                                                  : (state == State::Home ? mainContentBounds() : contentBounds());
  const bool showingShelf = state == State::Home && mainTab == MainTab::Shelf;
  const int shelfItems = showingShelf && shelfCount_ > 0 ? shelfItemsPerPage() : 0;
  const int shelfFrameSelection = shelfFrameSelection_;
  const int shelfFrameItems = shelfFrameItemsPerPage_;
  const bool shelfFrameInvalidated = showingShelf && shelfFrameInvalidated_.exchange(false);
  const bool incrementalShelfFrame =
      showingShelf && !shelfFrameInvalidated &&
      canIncrementShelfFrame(shelfFrameSelection, shelfFrameItems, shelfSelection, shelfItems);

  if (!incrementalShelfFrame) renderer.clearScreen();
  const char* header = tr(STR_WEREAD_TITLE);
  switch (state) {
    case State::Disclaimer:
      header = tr(STR_WEREAD_DISCLAIMER_TITLE);
      break;
    case State::Downloading:
      header = tr(STR_WEREAD_TAB_SHELF);
      break;
    case State::DetailLoading:
    case State::DetailCoverLoading:
    case State::Detail:
    case State::Introduction:
      header = tr(STR_WEREAD_BOOK_DETAIL);
      break;
    case State::Home:
    case State::Connecting:
    case State::Qr:
    case State::LoginConfirmed:
    case State::Syncing:
    case State::Cancelling:
    case State::OpenBook:
    case State::Error:
    case State::LogoutError:
    case State::ClearingCache:
    case State::CacheCleared:
    case State::CacheClearError:
      break;
  }
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, header);
  if (state == State::Home) {
    mainTabs_[0].selected = mainTab == MainTab::Shelf;
    mainTabs_[1].selected = mainTab == MainTab::Manage;
    GUI.drawTabBar(renderer,
                   Rect{safe.x, safe.y + metrics.topPadding + metrics.headerHeight, safe.width, metrics.tabBarHeight},
                   mainTabs_, mainFocus == MainFocus::Tabs);
  }

  switch (state) {
    case State::Disclaimer:
      drawDisclaimer(content);
      break;
    case State::Home:
      switch (mainTab) {
        case MainTab::Shelf:
          if (shelfCount_ == 0) {
            GUI.drawPopup(renderer, tr(STR_WEREAD_SHELF_EMPTY));
          } else {
            drawShelfGrid(content, shelfSelection, incrementalShelfFrame ? shelfFrameSelection : kNoShelfSelection,
                          mainFocus == MainFocus::Content);
          }
          break;
        case MainTab::Manage:
          GUI.drawButtonMenu(
              renderer, content, kManageEntryCount, mainFocus == MainFocus::Content ? manageSelected_ : -1,
              [](const int index) { return std::string(I18N.get(kManageEntries[index].title)); }, nullptr);
          break;
      }
      break;
    case State::Detail:
      drawBookDetail(content);
      break;
    case State::DetailCoverLoading:
      drawBookDetail(content, true);
      break;
    case State::DetailLoading:
      drawBookDetail(content);
      GUI.drawPopup(renderer, tr(STR_WEREAD_FETCHING_DETAIL));
      break;
    case State::Introduction:
      drawIntroduction(content);
      break;
    case State::Qr: {
      if (!qrUrl_[0]) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
        break;
      }
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int textGap = metrics.verticalSpacing;
      const int qrLimit = content.height - textGap - lineHeight * 2;
      const int qrSide = std::max(1, std::min(content.width * 4 / 5, qrLimit));
      const int groupHeight = qrSide + textGap + lineHeight * 2;
      const int qrY = content.y + std::max(0, (content.height - groupHeight) / 2);
      QrUtils::drawQrCode(renderer, Rect{content.x + (content.width - qrSide) / 2, qrY, qrSide, qrSide}, qrUrl_);
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap, tr(STR_WEREAD_SCAN_LOGIN));
      char target[64];
      snprintf(target, sizeof(target), "\"%s\"", tr(STR_WEREAD_TITLE));
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap + lineHeight, target);
      break;
    }
    case State::Syncing: {
      const auto stage = progressStage_.load();
      const uint32_t completed = progressCompleted_.load();
      const uint32_t total = progressTotal_.load();
      if (stage == WeReadClient::Operation::ProgressStage::Chapters) {
        if (completed > 0) {
          char status[96];
          snprintf(status, sizeof(status), tr(STR_WEREAD_SHELF_RECEIVED), static_cast<unsigned>(completed));
          GUI.drawPopup(renderer, status);
        } else {
          GUI.drawPopup(renderer, tr(STR_WEREAD_SYNCING_SHELF));
        }
        break;
      }
      if (total == 0) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
        break;
      }
      const char* label = nullptr;
      switch (stage) {
        case WeReadClient::Operation::ProgressStage::Preparing:
          label = tr(STR_WEREAD_FETCHING_COVER_INFO);
          break;
        case WeReadClient::Operation::ProgressStage::Images:
          label = tr(STR_WEREAD_DOWNLOADING_SHELF_COVERS);
          break;
        case WeReadClient::Operation::ProgressStage::Packaging:
          label = tr(STR_WEREAD_GENERATING_COVER_THUMBNAILS);
          break;
        case WeReadClient::Operation::ProgressStage::Chapters:
          break;
      }
      char status[64];
      snprintf(status, sizeof(status), "%s %u/%u", label ? label : "", static_cast<unsigned>(completed),
               static_cast<unsigned>(total));
      drawProgressStatus(renderer, content, operation_.progressTitle(), status, completed, total);
      break;
    }
    case State::Downloading: {
      const auto stage = progressStage_.load();
      const uint32_t completed = progressCompleted_.load();
      const uint32_t total = progressTotal_.load();
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      switch (stage) {
        case WeReadClient::Operation::ProgressStage::Preparing:
        case WeReadClient::Operation::ProgressStage::Packaging: {
          const StrId* lines = kPostProcessWaitingLines;
          int lineCount = static_cast<int>(sizeof(kPostProcessWaitingLines) / sizeof(kPostProcessWaitingLines[0]));
          switch (postProcessNotice_.load()) {
            case PostProcessNotice::None:
            case PostProcessNotice::Waiting:
              break;
            case PostProcessNotice::LongWait:
              lines = kPostProcessLongWaitLines;
              lineCount = static_cast<int>(sizeof(kPostProcessLongWaitLines) / sizeof(kPostProcessLongWaitLines[0]));
              break;
          }
          const int textGap = SubpageLayout::relatedGap(metrics);
          const int groupHeight = (lineCount + 1) * lineHeight + textGap;
          int y = content.y + std::max(0, (content.height - groupHeight) / 2);
          UITheme::drawCenteredText(renderer, content, UI_10_FONT_ID, y, pendingBook_.title);
          y += lineHeight + textGap;
          for (int i = 0; i < lineCount; ++i) {
            UITheme::drawCenteredText(renderer, content, UI_10_FONT_ID, y, I18N.get(lines[i]));
            y += lineHeight;
          }
          break;
        }
        case WeReadClient::Operation::ProgressStage::Chapters:
        case WeReadClient::Operation::ProgressStage::Images: {
          const char* label = stage == WeReadClient::Operation::ProgressStage::Chapters
                                  ? tr(STR_WEREAD_CACHING_CHAPTERS)
                                  : tr(STR_WEREAD_DOWNLOADING_IMAGES);
          char status[64];
          if (total == 0) {
            snprintf(status, sizeof(status), "%s", label);
          } else {
            snprintf(status, sizeof(status), "%s %u/%u", label, static_cast<unsigned>(completed),
                     static_cast<unsigned>(total));
          }
          drawProgressStatus(renderer, content, pendingBook_.title, status, completed, total);
          break;
        }
      }
      break;
    }
    case State::Error:
      GUI.drawPopup(renderer, errorMessage());
      break;
    case State::LogoutError:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGOUT_FAILED));
      break;
    case State::ClearingCache:
      GUI.drawPopup(renderer, tr(STR_CLEARING_CACHE));
      break;
    case State::CacheCleared:
      GUI.drawPopup(renderer, tr(STR_CACHE_CLEARED));
      break;
    case State::CacheClearError:
      GUI.drawPopup(renderer, tr(STR_CLEAR_CACHE_FAILED));
      break;
    case State::LoginConfirmed:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGIN_CONFIRMED));
      break;
    case State::Connecting:
      if (retryJob_ == Job::Sync) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_SYNCING_SHELF));
      } else {
        GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
      }
      break;
    case State::Cancelling:
    case State::OpenBook:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
      break;
  }

  const char* back = "";
  const char* confirm = "";
  const char* previous = "";
  const char* next = "";
  switch (state) {
    case State::Disclaimer:
      back = tr(STR_CANCEL);
      confirm = tr(STR_SELECT);
      previous = tr(STR_DIR_LEFT);
      next = tr(STR_DIR_RIGHT);
      break;
    case State::Home:
      back = tr(STR_BACK);
      switch (mainFocus) {
        case MainFocus::Tabs:
          confirm = tr(STR_SELECT);
          previous = tr(STR_DIR_LEFT);
          next = tr(STR_DIR_RIGHT);
          break;
        case MainFocus::Content:
          switch (mainTab) {
            case MainTab::Shelf:
              confirm = tr(STR_OPEN);
              previous = tr(STR_DIR_LEFT);
              next = tr(STR_DIR_RIGHT);
              break;
            case MainTab::Manage:
              confirm = tr(STR_SELECT);
              previous = tr(STR_DIR_UP);
              next = tr(STR_DIR_DOWN);
              break;
          }
          break;
      }
      break;
    case State::Detail:
    case State::DetailCoverLoading:
      back = tr(STR_BACK);
      confirm = tr(STR_SELECT);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case State::Introduction:
      back = tr(STR_BACK);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case State::Error:
    case State::LogoutError:
      back = tr(STR_BACK);
      confirm = state == State::Error && error_ == WeReadClient::Error::WholeBookOnly ? tr(STR_SELECT) : tr(STR_RETRY);
      break;
    case State::CacheCleared:
      back = tr(STR_BACK);
      break;
    case State::CacheClearError:
      back = tr(STR_BACK);
      confirm = tr(STR_RETRY);
      break;
    case State::Connecting:
    case State::Qr:
    case State::Syncing:
    case State::DetailLoading:
    case State::Downloading:
    case State::Cancelling:
      back = tr(STR_CANCEL);
      break;
    case State::LoginConfirmed:
    case State::OpenBook:
    case State::ClearingCache:
      break;
  }
  const auto labels = mappedInput.mapLabels(back, confirm, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (showingShelf) {
    if (shelfFrameInvalidated_.load()) {
      shelfFrameSelection_ = kNoShelfSelection;
      shelfFrameItemsPerPage_ = 0;
      requestUpdate(true);
      return;
    }
    if (shelfCount_ > 0) {
      // The framebuffer contains this snapshot even when the stale panel update below is skipped.
      shelfFrameSelection_ = shelfSelection;
      shelfFrameItemsPerPage_ = shelfItems;
      if (shelfSelected_.load() != shelfSelection) {
        requestUpdate(true);
        return;
      }
    } else {
      shelfFrameSelection_ = kNoShelfSelection;
      shelfFrameItemsPerPage_ = 0;
    }
  } else {
    shelfFrameSelection_ = kNoShelfSelection;
    shelfFrameItemsPerPage_ = 0;
  }
  if (state == State::Home &&
      (state_.load() != State::Home || mainTab_.load() != mainTab || mainFocus_.load() != mainFocus)) {
    requestUpdate(true);
    return;
  }
  renderer.displayBuffer();
}

bool WeReadActivity::preventAutoSleep() {
  const State state = state_.load();
  return isBusy(state) || (state == State::Home && operation_.active());
}
