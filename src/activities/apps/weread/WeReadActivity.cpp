#include "WeReadActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../../CrossPointState.h"
#include "../../../SdCardFontSystem.h"
#include "../../../SilentRestart.h"
#include "../../../components/UITheme.h"
#include "../../../components/icons/cover.h"
#include "../../../fontIds.h"
#include "../../../util/QrUtils.h"
#include "../../network/WifiSelectionActivity.h"
#include "../../util/ConfirmationActivity.h"

namespace {

static_assert(sizeof(WeReadClient::Operation) <= 8 * 1024, "WeRead workspace exceeds its fixed heap budget");

enum class MenuAction : uint8_t { Shelf, Refresh, Logout };

struct MenuEntry {
  StrId title;
  MenuAction action;
};

constexpr MenuEntry kMenuEntries[] = {
    {StrId::STR_WEREAD_MENU_SHELF, MenuAction::Shelf},
    {StrId::STR_WEREAD_MENU_REFRESH, MenuAction::Refresh},
    {StrId::STR_WEREAD_MENU_LOGOUT, MenuAction::Logout},
};

constexpr StrId kCacheScopeOptions[] = {
    StrId::STR_WEREAD_CACHE_WHOLE_BOOK,
    StrId::STR_WEREAD_CACHE_CHAPTER_RANGE,
};

constexpr int kMenuEntryCount = static_cast<int>(sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));
constexpr int kCoverWidth = 96;
constexpr int kCoverHeight = 140;

struct ShelfGridLayout {
  int columns = 1;
  int rows = 1;
  int itemsPerPage = 1;
  int tileWidth = kCoverWidth;
  int tileHeight = kCoverHeight;
  int startX = 0;
  int startY = 0;
  int spacing = 0;
};

ShelfGridLayout shelfGridLayout(GfxRenderer& renderer, const Rect& content, const int sidePadding, const int spacing) {
  ShelfGridLayout layout;
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID);
  layout.spacing = std::max(2, spacing);
  layout.tileWidth = kCoverWidth + layout.spacing * 2;
  layout.tileHeight = kCoverHeight + layout.spacing + titleHeight;
  const int availableWidth = std::max(1, content.width - sidePadding * 2);
  const int availableHeight = std::max(1, content.height);
  layout.columns = std::max(1, availableWidth / layout.tileWidth);
  layout.rows = std::max(1, (availableHeight + layout.spacing) / (layout.tileHeight + layout.spacing));
  layout.itemsPerPage = layout.columns * layout.rows;
  const int gridWidth = layout.columns * layout.tileWidth;
  const int gridHeight = layout.rows * layout.tileHeight + (layout.rows - 1) * layout.spacing;
  layout.startX = content.x + (content.width - gridWidth) / 2;
  layout.startY = content.y + std::max(0, (availableHeight - gridHeight) / 2);
  return layout;
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

    const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    switch (handleListTouch(selectedIndex_, chapterCount_, contentTop, contentHeight, false)) {
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

    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, chapterCount_, selectedIndex_,
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
  {
    RenderLock lock(*this);
    sdFontSystem.releaseLoadedFont(renderer);
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  }
  menuSelected_ = 0;
  shelfSelected_ = 0;
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
  cacheScopePopupClosing_ = false;
  // This bounded 832-byte probe is gone before TLS and avoids a transient heap
  // allocation that could fragment the ESP32-C3 heap.
  WeReadStore::Session session;
  const bool loggedIn = WeReadStore::loadSession(session);
  session.clear();
  if (loggedIn) {
    state_.store(State::Menu);
    requestUpdate();
  } else {
    syncShelf();
  }
}

void WeReadActivity::onExit() {
  operation_.reset();
  downloadRenderPending_.store(false);
  stageRenderPending_.store(false);
  if (shelfFile_.isOpen()) shelfFile_.close();
  Activity::onExit();
}

bool WeReadActivity::refreshShelf() {
  if (shelfFile_.isOpen()) shelfFile_.close();
  shelfCount_ = 0;
  if (!WeReadStore::openShelf(shelfFile_, shelfCount_)) {
    if (shelfFile_.isOpen()) shelfFile_.close();
    return false;
  }
  if (shelfCount_ == 0) {
    shelfSelected_ = 0;
  } else if (shelfSelected_ >= static_cast<int>(shelfCount_)) {
    shelfSelected_ = static_cast<int>(shelfCount_ - 1);
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
  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentH = renderer.getScreenHeight() - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return Rect{0, contentY, renderer.getScreenWidth(), std::max(0, contentH)};
}

int WeReadActivity::shelfItemsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return shelfGridLayout(renderer, contentBounds(), metrics.contentSidePadding, metrics.verticalSpacing).itemsPerPage;
}

void WeReadActivity::resetShelfCoverLoading() {
  if (state_.load() == State::Shelf) operation_.reset();
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
  startActivityForResult(std::move(wifi), [this, job](const ActivityResult& result) {
    (void)result;
    if (WiFi.status() == WL_CONNECTED) {
      startJob(job, job == Job::Sync ? nullptr : &pendingBook_);
      return;
    }
    error_ = WeReadClient::Error::Network;
    state_.store(State::Error);
    requestJobUpdate();
  });
}

void WeReadActivity::startJob(const Job job, const WeReadStore::ShelfRecord* book) {
  if (job == Job::Sync && shelfFile_.isOpen()) shelfFile_.close();
  if (job != Job::Sync && book) pendingBook_ = *book;
  retryJob_ = job;
  error_ = WeReadClient::Error::Ok;
  progressStage_.store(WeReadClient::Operation::ProgressStage::Chapters);
  progressCompleted_.store(0);
  progressTotal_.store(0);
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
  if (!operation_.begin(kind, book, options)) {
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
  return operation_.step();
}

void WeReadActivity::advanceShelfCovers() {
  if (shelfCoverStopped_ || shelfCount_ == 0 || WiFi.status() != WL_CONNECTED) return;
  const int itemsPerPage = shelfItemsPerPage();
  const int pageStart = shelfSelected_ / itemsPerPage * itemsPerPage;
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
        requestUpdate();
        return;
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

void WeReadActivity::advanceJob() {
  const auto event = stepOperation();
  if (retryJob_ == Job::Download) {
    const auto stage = operation_.progressStage();
    const uint32_t completed = operation_.progressCompleted();
    const uint32_t total = operation_.progressTotal();
    const auto previousStage = progressStage_.exchange(stage);
    const uint32_t previousCompleted = progressCompleted_.exchange(completed);
    const uint32_t previousTotal = progressTotal_.exchange(total);
    const bool stageChanged = previousStage != stage;
    const bool totalChanged = previousTotal != total;
    const bool completedChanged = previousCompleted != completed;
    const bool imageDecileChanged = stage == WeReadClient::Operation::ProgressStage::Images &&
                                    WeReadClient::Operation::progressDecile(previousCompleted, total) !=
                                        WeReadClient::Operation::progressDecile(completed, total);
    if (stageChanged) {
      stageRenderPending_.store(true);
      requestDownloadUpdate();
    } else if (totalChanged || (completedChanged && (stage == WeReadClient::Operation::ProgressStage::Chapters ||
                                                     imageDecileChanged || completed == total))) {
      requestDownloadUpdate();
    }
  }

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
          state_.store(State::Shelf);
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
      state_.store(retryJob_ == Job::Sync ? State::Menu : State::Shelf);
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
  if (readShelf(shelfSelected_, book)) openSelectedDetail(book);
}

bool WeReadActivity::detailActionEnabled(const DetailAction action) const {
  switch (action) {
    case DetailAction::Introduction:
      return detailIntroTruncated_;
    case DetailAction::Read:
      return Storage.exists(WeReadStore::finalBookPath(pendingBook_).c_str());
    case DetailAction::Cache:
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
    case DetailAction::Images:
      detailImagePolicy_ = detailImagePolicy_ == WeReadStore::ImagePolicy::Embed ? WeReadStore::ImagePolicy::Exclude
                                                                                 : WeReadStore::ImagePolicy::Embed;
      requestUpdate();
      return;
  }
}

void WeReadActivity::showCacheScopePopup() {
  cacheScopePopup_.show(StrId::STR_WEREAD_CACHE_BOOK, kCacheScopeOptions,
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
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (state_.load() == State::DetailCoverLoading) operation_.reset();
    state_.store(State::Shelf);
    requestUpdate();
  }
}

void WeReadActivity::handleIntroductionInput() {
  buttonNavigator_.onNext([this] {
    if (introPage_ + 1 < introPageCount_) {
      ++introPage_;
      requestUpdate();
    }
  });
  buttonNavigator_.onPrevious([this] {
    if (introPage_ > 0) {
      --introPage_;
      requestUpdate();
    }
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(retryJob_ == Job::Detail && operation_.active() ? State::DetailCoverLoading : State::Detail);
    requestUpdate();
  }
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
    state_.store(State::Shelf);
    requestUpdate();
    return;
  }
  syncShelf();
}

void WeReadActivity::syncShelf() {
  if (WiFi.status() == WL_CONNECTED) {
    startJob(Job::Sync);
  } else {
    connectThen(Job::Sync);
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

void WeReadActivity::performLogout() {
  operation_.reset();
  if (shelfFile_.isOpen()) shelfFile_.close();
  const bool sessionCleared = WeReadStore::clearSession();
  const bool shelfCleared = WeReadStore::clearShelf();
  shelfCount_ = 0;
  shelfSelected_ = 0;
  if (!sessionCleared || !shelfCleared) {
    LOG_ERR("WR", "Failed to clear local login state");
    state_.store(State::LogoutError);
    requestUpdate();
    return;
  }
  menuSelected_ = 0;
  syncShelf();
}

void WeReadActivity::handleMenuInput() {
  buttonNavigator_.onNext([this] {
    menuSelected_ = ButtonNavigator::nextIndex(menuSelected_, kMenuEntryCount);
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this] {
    menuSelected_ = ButtonNavigator::previousIndex(menuSelected_, kMenuEntryCount);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (kMenuEntries[menuSelected_].action) {
      case MenuAction::Shelf:
        openShelf();
        return;
      case MenuAction::Refresh:
        syncShelf();
        return;
      case MenuAction::Logout:
        promptLogout();
        return;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void WeReadActivity::handleShelfInput() {
  const int count = static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX));
  const int itemsPerPage = shelfItemsPerPage();
  buttonNavigator_.onNext([this, count, itemsPerPage] {
    const int previousPage = shelfSelected_ / itemsPerPage;
    shelfSelected_ = ButtonNavigator::nextIndex(shelfSelected_, count);
    if (shelfSelected_ / itemsPerPage != previousPage) resetShelfCoverLoading();
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this, count, itemsPerPage] {
    const int previousPage = shelfSelected_ / itemsPerPage;
    shelfSelected_ = ButtonNavigator::previousIndex(shelfSelected_, count);
    if (shelfSelected_ / itemsPerPage != previousPage) resetShelfCoverLoading();
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    resetShelfCoverLoading();
    activateSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    resetShelfCoverLoading();
    state_.store(State::Menu);
    requestUpdate();
  }
}

void WeReadActivity::handleErrorInput() {
  if (error_ == WeReadClient::Error::WholeBookOnly) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
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
    state_.store(retryJob_ == Job::Sync ? State::Menu : State::Shelf);
    requestJobUpdate();
  }
}

void WeReadActivity::handleLogoutErrorInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    performLogout();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(State::Menu);
    requestUpdate();
  }
}

void WeReadActivity::loop() {
  if (cacheScopePopup_.handleInput(mappedInput, [this] { requestUpdate(); })) {
    cacheScopePopupClosing_ = !cacheScopePopup_.isActive();
    return;
  }
  if (cacheScopePopupClosing_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;
    }
    cacheScopePopupClosing_ = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;
    }
  }

  const State state = state_.load();
  switch (state) {
    case State::Menu:
      handleMenuInput();
      return;
    case State::Shelf:
      handleShelfInput();
      if (state_.load() == State::Shelf) advanceShelfCovers();
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
         state == State::Downloading || state == State::Cancelling;
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

void WeReadActivity::drawShelfGrid(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = shelfGridLayout(renderer, content, metrics.contentSidePadding, metrics.verticalSpacing);
  const int count = static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX));
  const int page = shelfSelected_ / layout.itemsPerPage;
  const int pageStart = page * layout.itemsPerPage;
  const int pageEnd = std::min(pageStart + layout.itemsPerPage, count);

  for (int index = pageStart; index < pageEnd; ++index) {
    WeReadStore::ShelfRecord book;
    if (!readShelf(index, book)) continue;

    const int item = index - pageStart;
    const int tileX = layout.startX + item % layout.columns * layout.tileWidth;
    const int tileY = layout.startY + item / layout.columns * (layout.tileHeight + layout.spacing);
    const int coverX = tileX + (layout.tileWidth - kCoverWidth) / 2;
    bool coverDrawn = false;
    const std::string coverPath = WeReadStore::coverPath(WeReadStore::bookDirectory(book.bookId));
    if (Storage.exists(coverPath.c_str())) {
      HalFile file;
      if (Storage.openFileForRead("WR", coverPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bitmap, coverX, tileY, kCoverWidth, kCoverHeight);
          coverDrawn = true;
        }
      }
    }
    renderer.drawRect(coverX, tileY, kCoverWidth, kCoverHeight);
    if (!coverDrawn) {
      renderer.drawIcon(CoverIcon, coverX + (kCoverWidth - 32) / 2, tileY + (kCoverHeight - 32) / 2, 32);
    }

    const std::string title = renderer.truncatedText(SMALL_FONT_ID, book.title, layout.tileWidth - 4);
    const int titleWidth = renderer.getTextAdvanceX(SMALL_FONT_ID, title.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, tileX + std::max(0, (layout.tileWidth - titleWidth) / 2),
                      tileY + kCoverHeight + layout.spacing, title.c_str());
    if (index == shelfSelected_) {
      renderer.drawRect(tileX, tileY - 2, layout.tileWidth, layout.tileHeight + 4);
      renderer.drawRect(tileX + 1, tileY - 1, layout.tileWidth - 2, layout.tileHeight + 2);
    }
  }

  GUI.drawSideScrollBar(renderer, content, count, pageStart, layout.itemsPerPage);
}

void WeReadActivity::drawBookDetail(const Rect& content, const bool coverLoading) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const Rect cover{side, content.y, kCoverWidth, kCoverHeight};
  renderer.drawRect(cover.x, cover.y, cover.width, cover.height);
  bool coverDrawn = false;
  const std::string coverFile = WeReadStore::coverPath(WeReadStore::bookDirectory(pendingBook_.bookId));
  if (Storage.exists(coverFile.c_str())) {
    HalFile file;
    if (Storage.openFileForRead("WR", coverFile, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawBitmap(bitmap, cover.x + 2, cover.y + 2, cover.width - 4, cover.height - 4);
        coverDrawn = true;
      }
    }
  }
  if (!coverDrawn) {
    UITheme::drawCenteredWrappedText(
        renderer, cover, UI_10_FONT_ID,
        I18N.get(coverLoading ? StrId::STR_WEREAD_COVER_LOADING : StrId::STR_WEREAD_NO_COVER), 2);
  }

  const int metaX = cover.x + cover.width + 16;
  const int metaWidth = std::max(1, content.width - metaX - side);
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

  const int actionHeight = kDetailListActionCount * GUI.getListRowStep(true);
  const Rect actions{0, content.y + content.height - actionHeight, content.width, actionHeight};
  const int summaryY = cover.y + cover.height + metrics.verticalSpacing;
  const int summaryBottom = actions.y - metrics.verticalSpacing;
  const Rect introduction{side, summaryY, content.width - side * 2, std::max(1, summaryBottom - summaryY)};
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
          case DetailAction::Images:
            return std::string(tr(STR_WEREAD_CACHE_IMAGES));
        }
        return std::string();
      },
      [](const int) { return std::string(); }, nullptr,
      [this, cached](const int index) {
        switch (static_cast<DetailAction>(index + 1)) {
          case DetailAction::Introduction:
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
    renderer.drawText(UI_10_FONT_ID, side, content.y, tr(STR_WEREAD_DETAIL_UNAVAILABLE));
    return;
  }

  char line[192] = {};
  size_t lineLength = 0;
  int lineWidth = 0;
  int y = content.y;
  uint32_t offset = start;
  const auto flushLine = [&]() {
    line[lineLength] = '\0';
    if (lineLength > 0) renderer.drawText(UI_10_FONT_ID, side, y, line);
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
    renderer.drawText(UI_10_FONT_ID, side, y, "...");
  }
  char page[32];
  snprintf(page, sizeof(page), tr(STR_WEREAD_PAGE_FMT), static_cast<unsigned>(introPage_ + 1),
           static_cast<unsigned>(introPageCount_));
  renderer.drawCenteredText(SMALL_FONT_ID, footerY, page);
}

void WeReadActivity::render(RenderLock&&) {
  downloadRenderPending_.store(false);
  stageRenderPending_.store(false);
  if (cacheScopePopup_.processRender(renderer, mappedInput)) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const Rect content = contentBounds();
  const State state = state_.load();

  renderer.clearScreen();
  const char* header = tr(STR_WEREAD_TITLE);
  switch (state) {
    case State::Shelf:
    case State::Downloading:
      header = tr(STR_WEREAD_MENU_SHELF);
      break;
    case State::DetailLoading:
    case State::DetailCoverLoading:
    case State::Detail:
    case State::Introduction:
      header = tr(STR_WEREAD_BOOK_DETAIL);
      break;
    case State::Menu:
    case State::Connecting:
    case State::Qr:
    case State::LoginConfirmed:
    case State::Syncing:
    case State::Cancelling:
    case State::OpenBook:
    case State::Error:
    case State::LogoutError:
      break;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, header);

  switch (state) {
    case State::Menu:
      GUI.drawButtonMenu(
          renderer, content, kMenuEntryCount, menuSelected_,
          [](const int index) { return std::string(I18N.get(kMenuEntries[index].title)); }, nullptr);
      break;
    case State::Shelf:
      if (shelfCount_ == 0) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_SHELF_EMPTY));
      } else {
        drawShelfGrid(content);
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
      QrUtils::drawQrCode(renderer, Rect{(width - qrSide) / 2, qrY, qrSide, qrSide}, qrUrl_);
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap, tr(STR_WEREAD_SCAN_LOGIN));
      char target[64];
      snprintf(target, sizeof(target), "\"%s\"", tr(STR_WEREAD_TITLE));
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap + lineHeight, target);
      break;
    }
    case State::Downloading: {
      const auto stage = progressStage_.load();
      const uint32_t completed = progressCompleted_.load();
      const uint32_t total = progressTotal_.load();
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int centerY = (height - lineHeight) / 2;
      char status[64];
      const char* label = nullptr;
      switch (stage) {
        case WeReadClient::Operation::ProgressStage::Chapters:
          label = tr(STR_WEREAD_CACHING_CHAPTERS);
          break;
        case WeReadClient::Operation::ProgressStage::Images:
          label = tr(STR_WEREAD_DOWNLOADING_IMAGES);
          break;
        case WeReadClient::Operation::ProgressStage::Packaging:
          label = tr(STR_WEREAD_PACKAGING_BOOK);
          break;
      }
      if (total == 0 || stage == WeReadClient::Operation::ProgressStage::Packaging) {
        snprintf(status, sizeof(status), "%s", label);
      } else {
        snprintf(status, sizeof(status), "%s %u/%u", label, static_cast<unsigned>(completed),
                 static_cast<unsigned>(total));
      }
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight - metrics.verticalSpacing, pendingBook_.title);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, status);
      if (total > 0 && stage != WeReadClient::Operation::ProgressStage::Packaging) {
        const int barY = centerY + lineHeight + metrics.verticalSpacing;
        GUI.drawProgressBar(
            renderer,
            Rect{metrics.contentSidePadding, barY, width - metrics.contentSidePadding * 2, metrics.progressBarHeight},
            completed, total);
      }
      break;
    }
    case State::Error:
      GUI.drawPopup(renderer, errorMessage());
      break;
    case State::LogoutError:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGOUT_FAILED));
      break;
    case State::LoginConfirmed:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGIN_CONFIRMED));
      break;
    case State::Connecting:
    case State::Syncing:
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
    case State::Menu:
      back = tr(STR_BACK);
      confirm = tr(STR_SELECT);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case State::Shelf:
      back = tr(STR_BACK);
      confirm = tr(STR_OPEN);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
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
      break;
  }
  const auto labels = mappedInput.mapLabels(back, confirm, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool WeReadActivity::preventAutoSleep() {
  const State state = state_.load();
  return isBusy(state) || (state == State::Shelf && operation_.active());
}
