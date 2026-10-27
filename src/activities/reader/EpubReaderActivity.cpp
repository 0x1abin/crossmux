#include "EpubReaderActivity.h"

#include <BlePageTurner.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/css/CssParser.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SdCardFontCache.h>
#include <TrustedTime.h>
#include <Txt.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <variant>

#include "AchievementsStore.h"
#include "BleInput.h"
#include "BookmarkEntry.h"
#include "ChapterBuildGate.h"
#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFont.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/FontPreloadView.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkFile.h"
#include "util/ReadingBackground.h"
#include "util/ReadingGuideLine.h"
#ifdef ENABLE_CHINESE_VERSION
#include <WeReadStore.h>

#include "activities/apps/weread/WeReadProgressSyncActivity.h"
#include "activities/settings/FontDownloadActivity.h"
#endif
#include "clippings/ClippingText.h"
#include "clippings/ClippingsManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/BookmarkUtil.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/TimeUtils.h"

namespace {
constexpr uint8_t MAX_PAGE_TURN_RATE = 30;
// The X4 Pro and X4 Classic carry the X4's panel but sit outside isXteinkDevice()
// (that helper also gates power management). Overlay refresh choices are per-panel:
// this family runs the grayscale anti-aliasing pass, so chrome painted over a
// fresh page needs the HALF ghost-cleanup and closing re-renders the page.
// EEGO A4 runs the same grayscale AA pass on its UC8279C 4-gray panel, so its
// overlay paints need the identical full-waveform treatment: a FAST differential
// after the AA waveform leaves the covered page ghosting through the chrome.
bool xteinkClassPanel() {
  return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic() ||
         display.getController() == HalDisplay::Controller::UC8279C;
}

constexpr int PAGE_TURN_RATES[] = {0, 3, 6, 12};
constexpr float bookmarkProgressEpsilon = 0.0001f;
constexpr int CLIP_SELECTION_PAGE_WINDOW = 3;

// Word i's font: headings and CSS font-size runs can differ from the section font.
int clippingWordFont(const GfxRenderer& renderer, const int fontId, const TextBlock& block, const uint16_t i) {
  return TextBlock::renderFontId(renderer, block.wordFontId(i, fontId), fontId);
}

Rect clippingWordRect(const GfxRenderer& renderer, const int fontId, const PageLine& line, const uint16_t i,
                      const int top, const int left, const int wordWidth) {
  const auto& block = line.getBlock();
  const int wordFont = clippingWordFont(renderer, fontId, *block, i);
  const char* text = block->wordText(i);
  const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
  const int skipX =
      clippingText::hasEmSpacePrefix(text) ? renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", style) : 0;
  int width = wordWidth - skipX;
  if (i + 1 < block->wordCount() && block->wordXpos(i + 1) > block->wordXpos(i)) {
    width = std::min(width, static_cast<int>(block->wordXpos(i + 1) - block->wordXpos(i) - skipX));
  }
  const int lineFont = TextBlock::renderFontId(renderer, block->blockFontId(fontId), fontId);
  return Rect{left + line.xPos + block->wordXpos(i) + skipX,
              top + line.yPos + block->getRubyShift(renderer.getFontAscenderSize(lineFont)) +
                  block->wordYOffset(renderer, fontId, i),
              width, renderer.getLineHeight(wordFont)};
}

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

constexpr char READ_FOLDER[] = "/read";

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  const auto moved = moveBookFile(srcPath, dstPath);
  if (moved == BookMoveResult::NotMoved) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  if (moved == BookMoveResult::MovedDataIncomplete) {
    LOG_ERR("ERS", "Finished-book data migration incomplete: %s -> %s", srcPath.c_str(), dstPath.c_str());
  }
}

int getChapterTocIndex(const Epub& epub, const int spineIndex, std::optional<uint32_t> visibleOffset,
                       uint8_t* chapterProgress = nullptr) {
  if (Txt::isTxtOrMd(epub.getPath()) && visibleOffset) {
    const int index = Txt::tocIndexForPosition(epub.getPath(), epub.getCachePath(), *visibleOffset, chapterProgress);
    if (index >= 0 && index < epub.getTocItemsCount()) return index;
  }
  return epub.getTocIndexForSpineIndex(spineIndex);
}

std::string getStatsChapterTitle(const Epub& epub, const int spineIndex, std::optional<uint32_t> visibleOffset,
                                 uint8_t* chapterProgress = nullptr) {
  int tocIndex = getChapterTocIndex(epub, spineIndex, visibleOffset, chapterProgress);
  if (tocIndex < 0) {
    int nearestTocIndex = -1;
    int nearestSpineIndex = -1;
    for (int index = 0; index < epub.getTocItemsCount(); ++index) {
      const int tocSpineIndex = epub.getSpineIndexForTocIndex(index);
      if (tocSpineIndex <= spineIndex && tocSpineIndex >= nearestSpineIndex) {
        nearestSpineIndex = tocSpineIndex;
        nearestTocIndex = index;
      }
    }
    tocIndex = nearestTocIndex;
  }
  return tocIndex < 0 ? "" : epub.getTocItem(tocIndex).title;
}

uint8_t getStatsChapterProgressPercent(const int currentPage, const int pageCount) {
  if (pageCount <= 0) return 0;
  return static_cast<uint8_t>(clampPercent(
      static_cast<int>((static_cast<float>(currentPage + 1) / static_cast<float>(pageCount)) * 100.0f + 0.5f)));
}

// A button contact is closed right now. Reads the Xteink ADC ladder and power pin only; on other
// boards a press stops the build once the loop has debounced it.
bool buttonContactClosed() { return gpio.rawInputActive(); }

}  // namespace

bool EpubReaderActivity::needsPartialRebuild() const {
  return section && !section->isBuilding() && section->isPartial() && failedBuildSpine_ != currentSpineIndex &&
         section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount);
}

bool EpubReaderActivity::deferBluetoothStart() const {
  // Preparation blocks a new host, not an existing reader/menu connection.
  return !section || section->isBuilding() || section->pageCount == 0 || needsPartialRebuild();
}

void EpubReaderActivity::prepareChapterBuild() {
  // C3 and PSRAM hosts retain existing links; parser memory guards still apply.
#if FREEINK_CAP_BLE_HID_HOST && !CROSSPOINT_BLE_HOST_PSRAM && !CONFIG_IDF_TARGET_ESP32C3
  bleinput::stop();
#endif
#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
  // Reclaim arenas before parser allocations interleave with them; retain the
  // selected font and coverage index for both layout and subsequent reading.
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
#endif
}

bool EpubReaderActivity::updateChapterBuild() {
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return true;
  if (!section) return true;

  if (needsPartialRebuild() && buildViewportWidth > 0) {
    const ReaderRenderSpec buildSpec = effectiveRenderSpec(buildViewportWidth, buildViewportHeight);
    prepareChapterBuild();
    if (!section->startBuild(buildSpec)) {
      if (!handleBuildFailure("deferred partial start", section->buildError())) requestUpdate();
      return false;
    }
    LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
            section->pageCount);
  }
  if (!section->isBuilding()) return true;

  bool suspendForBluetooth = false;
#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
  suspendForBluetooth = SETTINGS.ble.enabled;
#endif
  // A paused parser still owns its heap. Build beyond the restart margin, then
  // persist a partial cache and release the parser to bound its lifetime.
  const int buildWindow = BUILD_WINDOW_AHEAD + (suspendForBluetooth ? PARTIAL_REBUILD_START_MARGIN : 0);
  const bool pendingReposition = cachedVisibleTextOffset.has_value() || cachedChapterTotalPageCount != 0;
  if (((!suspendForBluetooth && section->isPartial()) || (suspendForBluetooth && pendingReposition) ||
       static_cast<int>(section->pageCount) < section->currentPage + buildWindow) &&
      buildTickHeapGate()) {
    if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
      if (!handleBuildFailure("background build", section->buildError())) requestUpdate();
      return false;
    }
    if (section->isBuildComplete() && applyDeferredReposition()) requestUpdate();
  }

  if (suspendForBluetooth && section->isBuilding() && !pendingReposition &&
      static_cast<int>(section->pageCount) >= section->currentPage + buildWindow) {
    section->suspendBuild();
    if (section->buildError() != Section::BuildError::None) {
      if (!handleBuildFailure("partial suspend", section->buildError())) requestUpdate();
      return false;
    }
  }
  return true;
}

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  settleOverlayRefresh();
  discardOverlayPage();  // free the overlay's page snapshot if one is held
  // A radio still stopped for this book's build may start again in the next one.
  if (radioReleasedForBuild.load()) bleturner::afterPaint();

  if (footnoteDepth > 0 && epub) saveLinkStack();

  section.reset();
  CLIPPINGS.unload();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();
    moveFinishedBookToReadFolder(srcPath, dstPath);
  } else {
    epub.reset();
  }
}

void EpubReaderActivity::onExit() {
  if (isViewingNonlinearSpine()) {
    const auto position = chapterPosition();
    saveProgress(currentSpineIndex, position.pageIndex, position.totalPages);
  } else if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, origin.pageCount);
  }

  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
#if FREEINK_DEVICE_READPICO
  // Four frames of PSRAM; the reader is the only owner and gives them back here.
  freePageCache();
#endif
  showPendingAchievementPopups(renderer);
  ReaderActivity::onExit();
}

bool EpubReaderActivity::loadBook() {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
#if FREEINK_CAP_BLE_HID_HOST && !CROSSPOINT_BLE_HOST_PSRAM && !CONFIG_IDF_TARGET_ESP32C3
    bleinput::stop();
#endif
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (!loaded) {
    // Surfaced by handleLoadFailure() as a dialog; loadedEpub dies with this
    // scope, so carry the reason out in a member.
    loadProtectionError = loadedEpub->getProtectionError();
    LOG_ERR("ERS", "Failed to load EPUB%s%s", loadProtectionError.empty() ? "" : ": ", loadProtectionError.c_str());
    return false;
  }
  epub = std::move(loadedEpub);

  ImageBlock::clearSessionRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest, CancelCheck cancellation) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest, cancellation);
  });

  epub->setupCacheDir();

#ifdef ENABLE_CHINESE_VERSION
  wereadBookId_[0] = '\0';
  if (WeReadStore::findBookIdForPath(epub->getPath(), wereadBookId_, sizeof(wereadBookId_)) &&
      strncmp(wereadBookId_, "MP_WXS_", 7) == 0) {
    wereadBookId_[0] = '\0';
  }
  if (wereadBookId_[0]) {
    const uint32_t timestamp = TimeUtils::getCurrentValidTimestamp();
    if (timestamp != 0) {
      switch (WeReadStore::promoteShelfBook(wereadBookId_, timestamp)) {
        case WeReadStore::ShelfSortResult::Ok:
          break;
        case WeReadStore::ShelfSortResult::Degraded:
          LOG_DBG("WR", "Large shelf: recent book promotion deferred until sync");
          break;
        case WeReadStore::ShelfSortResult::StorageError:
          LOG_ERR("WR", "Failed to promote recently opened shelf book");
          break;
      }
    }
  }
#endif
  bool hasSavedProgress = false;

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    int dataSize = f.read(data, sizeof(data));
    if (dataSize == 4 || dataSize == 6 || dataSize == 10) {
      hasSavedProgress = true;
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    } else if (dataSize == 10) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      cachedVisibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                                (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
    }
  }

  if (!hasSavedProgress && Txt::isTxtOrMd(bookPath)) {
    uint32_t visibleOffset = 0;
    switch (Txt::restoreLegacyProgress(bookPath, epub->getCachePath(), visibleOffset)) {
      case txt_progress::LegacyResult::Absent:
        break;
      case txt_progress::LegacyResult::Restored:
        currentSpineIndex = cachedSpineIndex = 0;
        nextPageNumber = 0;
        cachedVisibleTextOffset = pendingOffsetJump = visibleOffset;
        legacyProgressPending = true;
        break;
      case txt_progress::LegacyResult::Failed:
        legacyProgressPending = true;
        loadProtectionError = "txt progress migration failed";
        return false;
    }
  }

  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex > 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  if (isViewingNonlinearSpine()) {
    const int firstLinear = epub->getSpineIndexForLinearIndex(0);
    if (firstLinear < 0) return false;
    currentSpineIndex = cachedSpineIndex = firstLinear;
    nextPageNumber = cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
  }

#ifdef ENABLE_CHINESE_VERSION
  if (wereadBookId_[0]) {
    float initialProgress = 0.0f;
    const bool loaded = WeReadStore::loadInitialProgress(wereadBookId_, initialProgress);
    if (hasSavedProgress || !loaded || initialProgress <= 0.0f) {
      WeReadStore::clearInitialProgress(wereadBookId_);
    } else if (jumpToFraction(initialProgress)) {
      clearInitialProgressAfterSave_ = true;
    } else {
      WeReadStore::clearInitialProgress(wereadBookId_);
    }
  }
#endif

  READING_STATS.beginSession(
      epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getCoverBmpPath(),
      clampPercent(static_cast<int>(epub->calculateProgress(currentSpineIndex, 0.0f) * 100.0f + 0.5f)),
      getStatsChapterTitle(*epub, currentSpineIndex, cachedVisibleTextOffset), 0);

  loadLinkStack();
  loadCachedBookmarks();
  if (!CLIPPINGS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub")) {
    LOG_ERR("CLIP", "Failed to load clippings for %s", epub->getPath().c_str());
  }
  return true;
}

ChapterPosition EpubReaderActivity::chapterPosition() const {
  if (section) return {section->currentPage, section->estimatedTotalPages()};
  return {nextPageNumber, cachedChapterTotalPageCount};
}

float EpubReaderActivity::bookProgressFor(const ChapterPosition& position, const bool includeCurrentPage) const {
  if (!epub || epub->getBookSize() == 0) return 0.0f;
  int spineIndex = currentSpineIndex;
  ChapterPosition readingPosition = position;
  if (isViewingNonlinearSpine()) {
    const auto origin = getReadingPosition();
    spineIndex = origin.spineIndex;
    readingPosition = {origin.pageNumber, origin.totalPages};
  }
  if (spineIndex < 0 || !readingPosition.hasTotal()) return 0.0f;
  if (includeCurrentPage) ++readingPosition.pageIndex;
  // The page index can run past the chapter's estimated total while it is still
  // building, so the fraction is clamped before the cast.
  return std::clamp(epub->calculateProgress(spineIndex, readingPosition.chapterFraction()), 0.0f, 1.0f);
}

int EpubReaderActivity::bookPercentFor(const ChapterPosition& position) const {
  return static_cast<int>(bookProgressFor(position) * 100.0f + 0.5f);
}

void EpubReaderActivity::openReaderMenu() {
  pendingManualTurn = 0;
  if (usesToolbarMenu()) {
    // Reached from a child activity's result handler (footnotes, bookmarks,
    // go-to-percent... cancelled back to the menu), so the framebuffer holds
    // that screen, not the page: re-render the page and let renderBook() put
    // the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = makeUniqueNoThrow<ReaderToolbarUi>(renderer);
    if (!toolbarUi) {
      LOG_ERR("ERS", "OOM allocating reader toolbar");
      overlay = Overlay::None;
      return;
    }
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }
  // Child screens (chapter list, text settings) release the section to free its
  // pagination buffers; chapterPosition() covers that with the cached position.
  const ChapterPosition position = chapterPosition();
  const int currentPage = position.displayPage();
  const int totalPages = position.totalPages;
  const int bookProgressPercent = bookPercentFor(position);
  startActivityForResultWith<EpubReaderMenuActivity>(
      [this](const ActivityResult& result) {
        READING_STATS.resumeSession();
        const auto& menu = std::get<MenuResult>(result.data);
        // The menu leaves night mode / frontlight in SETTINGS unsaved (it edits
        // them in place). Persist them here, where the global typography
        // snapshot can be restored first so a book's overrides never reach the
        // settings file.
        if (menu.settingsDirty) {
          saveSettingsKeepingBookStyle();
        }
        if (SETTINGS.orientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }
        toggleAutoPageTurn(menu.pageTurnRate);
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
        }
      },
      epub->getTitle(), currentPage, totalPages, bookProgressPercent, SETTINGS.orientation,
      !currentPageFootnotes.empty(), !cachedBookmarks.empty(), CLIPPINGS.hasClippings());
}

ReaderRenderSpec EpubReaderActivity::effectiveRenderSpec(const uint16_t width, const uint16_t height) const {
  auto spec = SETTINGS.readerRenderSpec(width, height);
  spec.collectTouchLinks = mappedInput.hasTouch();
  if (stylesDisabledForSession_) spec.embeddedStyle = false;
  return spec;
}

bool EpubReaderActivity::handleBuildFailure(const char* stage, const Section::BuildError error) {
  if (failedBuildSpine_ == currentSpineIndex) return false;
  bool retry = false;
#ifndef BOARD_HAS_PSRAM
  retry = error == Section::BuildError::OutOfMemory && !stylesDisabledForSession_ && SETTINGS.embeddedStyle != 0;
#endif
  if (retry) {
    // A page number is not a content position after CSS changes. Explicit
    // anchor/percentage/offset requests retain precedence over the displayed page.
    if (pendingPageJump == std::numeric_limits<uint16_t>::max()) {
      pendingPercentJump = true;
      pendingSpineProgress = 1.0f;
    }
    if (pendingAnchor.empty() && !pendingPercentJump && !pendingOffsetJump) {
      if (renderedSpineIndex_ == currentSpineIndex && currentPageVisibleOffset) {
        pendingOffsetJump = currentPageVisibleOffset;
      } else if (cachedSpineIndex == currentSpineIndex && cachedVisibleTextOffset) {
        pendingOffsetJump = cachedVisibleTextOffset;
      }
    }
    pendingPageJump.reset();
    clearDeferredReposition();
    nextPageNumber = 0;
    stylesDisabledForSession_ = true;
  }
  if (section) section->abandonBuild();
  section.reset();
  if (auto* css = epub->getCssParser()) css->clear();
  discardOverlayPage();
  currentPageLinks.clear();
  currentPageFootnotes.resize(0);
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  buildHeapPaused = false;
  buildPopupPending = false;
  failedBuildSpine_ = retry ? -1 : currentSpineIndex;
  LOG_ERR("ERS", "Build failed: spine=%d stage=%s error=%u retryWithoutCss=%d (free=%u, min=%u, maxAlloc=%u)",
          currentSpineIndex, stage, static_cast<unsigned>(error), retry, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (retry) {
    requestUpdate();
  } else {
    automaticPageTurnActive = false;
  }
  return retry;
}

// Wait for lifecycle changes before sharing the heap with a chapter build. Existing C3/PSRAM
// links stay alive under the fork's parser memory guards; internal-only S3 releases the radio.
bool EpubReaderActivity::makeRoomForChapterBuild() {
#if CONFIG_IDF_TARGET_ESP32C3 || CROSSPOINT_BLE_HOST_PSRAM
  bleturner::settleStart();
  const auto ready = [] {
    const auto st = bleturner::status();
    return chapterBuildMayRun(st, true);
  };
  return waitForBuildRoom(
      ready, [] { return static_cast<uint32_t>(millis()); }, [](uint32_t ms) { delay(ms); }, BUILD_ROOM_WAIT_MS,
      BUILD_ROOM_STEP_MS);
#endif
  bool asked = false;
  const auto roomMade = [this, &asked] {
    // The first ask stops the radio. A later one asks again only once a start that was still
    // settling has left the radio up.
    if (!radioReleasedForBuild.load() && (!asked || !bleturner::status().starting)) {
      radioReleasedForBuild = bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
    }
    asked = true;
    return chapterBuildMayRun(bleturner::status());
  };
  const auto nowMs = [] { return static_cast<uint32_t>(millis()); };
  const auto nap = [](const uint32_t ms) { delay(ms); };
  if (waitForBuildRoom(roomMade, nowMs, nap, BUILD_ROOM_WAIT_MS, BUILD_ROOM_STEP_MS)) return true;
  LOG_ERR("ERS", "Bluetooth radio still holds the heap after %u ms; chapter build refused",
          static_cast<unsigned>(BUILD_ROOM_WAIT_MS));
  return false;
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
#ifndef BOARD_HAS_PSRAM
  if (buildHeapPaused && !stylesDisabledForSession_ && SETTINGS.embeddedStyle != 0) return true;
#endif
  return !buildHeapPaused;
}

void EpubReaderActivity::showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  pagesUntilFullRefresh = 1;
  buildPopupPending = false;
}

// Debounced state of every button. Covers the frame between a raw release and its debounced
// release edge, which would otherwise run a build step before the page turn.
bool EpubReaderActivity::anyButtonPressed() const {
  using Button = MappedInputManager::Button;
  for (const Button button :
       {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down, Button::Power}) {
    if (mappedInput.isPressed(button)) return true;
  }
  return false;
}

void EpubReaderActivity::backgroundBuildTick() {
  RenderLock lock(RenderLock::Mode::Try);
  // Not under an open toolbar or panel: those repaint straight onto the page in the framebuffer.
  if (!lock.ownsLock() || overlay != Overlay::None || !backgroundBuildWanted()) return;
  // A button is down or a page turn is queued: the next loop frames act on it first.
  if (buttonContactClosed() || anyButtonPressed() || pendingManualTurn != 0 || !buildTickHeapGate()) return;
  // A build step can lend the framebuffer (image probes), which hands it back white while the
  // panel still shows the page; redraw so nothing is later painted over the blank buffer.
  const uint32_t loansBefore = renderer.frameBufferLoanCount();
  const bool built = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK, &buttonContactClosed);
  if (renderer.frameBufferLoanCount() != loansBefore) {
    pageBufferStale = true;
    requestUpdate();
  }
  if (!built) {
    LOG_ERR("ERS", "Background section build failed");
    backgroundBuildFailedSpine = currentSpineIndex;
    nextPageNumber = section->currentPage;
    section.reset();
    requestUpdate();
  } else if (section->isBuildComplete() && applyDeferredReposition()) {
    requestUpdate();
  }
}

void EpubReaderActivity::openDictionaryWordSelect() {
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  startClipSelection(-1, -1, ClipSelectionActivity::Mode::Lookup);
}

void EpubReaderActivity::startClipSelection(const int initialX, const int initialY,
                                            const ClipSelectionActivity::Mode mode) {
  if (!section || !epub || section->currentPage < 0 || section->currentPage >= section->pageCount) return;

  invalidateClippingRender();
  const int pageNumber = section->currentPage;
  const uint16_t pageCount = section->pageCount;
  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  std::vector<std::unique_ptr<Page>> pages;
  pages.reserve(CLIP_SELECTION_PAGE_WINDOW);
  for (int pageOffset = 0; pageOffset < CLIP_SELECTION_PAGE_WINDOW && pageNumber + pageOffset < pageCount;
       ++pageOffset) {
    auto page = section->loadPage(pageNumber + pageOffset);
    if (!page) break;
    if (pageOffset == 0 && initialX >= 0 && CLIPPINGS.hasClippings()) {
      RenderLock lock;
      const int clippingIndex = clippingAtPoint(*page, initialX, initialY);
      if (clippingIndex >= 0) {
        const bool removed = CLIPPINGS.removeClippingAt(static_cast<size_t>(clippingIndex));
        if (!removed) LOG_ERR("CLIP", "Failed to delete highlighted clipping %d", clippingIndex);
        clippingMessage = removed ? StrId::STR_CLIPPING_REMOVED : StrId::STR_CLIPPING_DELETE_FAILED;
        showClippingMessage = true;
        clippingMessageTime = millis();
        requestUpdate();
        return;
      }
    }
    pages.push_back(std::move(page));
  }
  if (pages.empty()) {
    LOG_ERR("CLIP", "Failed to load page %d for clipping selection", pageNumber);
    requestUpdate();
    return;
  }

  if (buildViewportWidth == 0 || buildViewportHeight == 0) {
    LOG_ERR("CLIP", "Cannot anchor clipping before the reader viewport is initialized");
    requestUpdate();
    return;
  }
  const uint32_t layoutSignature =
      readerRenderSpecSignature(effectiveRenderSpec(buildViewportWidth, buildViewportHeight));

  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;

  std::string chapterTitle;
  const int tocIndex = currentTocIndex();
  if (tocIndex >= 0) chapterTitle = epub->getTocItem(tocIndex).title;
  std::string bookTitle = epub->getTitle();
  std::string author = epub->getAuthor();

  auto activity = makeUniqueNoThrow<ClipSelectionActivity>(renderer, mappedInput, std::move(pages), marginLeft,
                                                           marginTop, initialX, initialY, mode);
  if (!activity) {
    LOG_ERR("CLIP", "Failed to allocate clipping selection activity");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity), [this, spineIndex, pageNumber, pageCount, layoutSignature,
                                               bookTitle = std::move(bookTitle), author = std::move(author),
                                               chapterTitle = std::move(chapterTitle)](const ActivityResult& result) {
    invalidateClippingRender();
    RenderLock lock;
    READING_STATS.resumeSession();
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& clipping = std::get<ClippingResult>(result.data);
    const uint16_t startPage = static_cast<uint16_t>(pageNumber + clipping.startPageOffset);
    const uint16_t endPage = static_cast<uint16_t>(pageNumber + clipping.endPageOffset);
    if (section && currentSpineIndex == spineIndex && endPage < section->pageCount) {
      section->currentPage = endPage;
      currentPageVisibleOffset = section->getVisibleTextOffsetForPage(endPage);
    }
    if (clipping.action == ClippingResult::Action::Bookmark) {
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return;
    }
    if (clipping.action == ClippingResult::Action::Lookup) {
      auto page = section ? section->loadPage(section->currentPage) : nullptr;
      if (!page) {
        LOG_ERR("CLIP", "Failed to load lookup page");
        requestUpdate();
        return;
      }
      int top, right, bottom, left;
      renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
      auto lookup = makeUniqueNoThrow<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page),
                                                                    left + SETTINGS.screenMargin,
                                                                    top + SETTINGS.screenMargin, clipping.text);
      if (!lookup) {
        LOG_ERR("CLIP", "OOM: dictionary lookup activity");
        requestUpdate();
        return;
      }
      startActivityForResult(std::move(lookup), [this](const ActivityResult& result) {
        if (!result.isCancelled) openDictionaryWordSelect();
      });
      return;
    }
    const uint16_t paragraphIndex =
        section ? section->getParagraphIndexForPage(startPage).value_or(UINT16_MAX) : UINT16_MAX;
    const auto addResult = CLIPPINGS.addClipping(
        spineIndex, startPage, endPage, pageCount, clipping.startWordIndex, clipping.endWordIndex, clipping.wordCount,
        chapterTitle.c_str(), paragraphIndex, clipping.text, layoutSignature, clipping.startOffset, clipping.endOffset);
    bool exported = false;
    if (addResult == ClippingStore::AddResult::Added) {
      exported = ClippingsManager::saveClipping(bookTitle, author, chapterTitle, startPage + 1, clipping.text);
    }
    clippingMessage = addResult == ClippingStore::AddResult::LimitReached ? StrId::STR_CLIPPING_LIMIT_REACHED
                      : exported                                          ? StrId::STR_CLIPPING_SAVED
                      : addResult == ClippingStore::AddResult::Added      ? StrId::STR_CLIPPING_EXPORT_FAILED
                                                                          : StrId::STR_CLIPPING_FAILED;
    showClippingMessage = true;
    clippingMessageTime = millis();
    requestUpdate();
  });
}

#ifdef ENABLE_CHINESE_VERSION
bool EpubReaderActivity::maybeOfferCompleteChineseFont() {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    pendingMissingChineseCodepoint_.store(0, std::memory_order_relaxed);
    return false;
  }

  const uint32_t codepoint = pendingMissingChineseCodepoint_.exchange(0, std::memory_order_relaxed);
  if (codepoint == 0 || FontDownloadActivity::wasChineseFontPromptShownThisBoot()) return false;

  LOG_INF("FONT", "Missing built-in Chinese glyph U+%04X; offering automatic NotoSansSC install",
          static_cast<unsigned>(codepoint));
  // The installer writes SETTINGS, so it gets this session's baseline and the
  // book's live typography: the installed family becomes global, the book's
  // other typography fields stay out of the settings file. The pointer must
  // outlive the child activity that stores it.
  //
  // saveFontSelection() swaps this snapshot back in after its write, so it has
  // to be taken at save time, not here: the family the user picks does not exist
  // yet when the installer opens, and restoring this pre-install snapshot would
  // erase the very selection that was just written.
  fontInstallLiveStyle_ = captureStyleFromSettings();
  auto downloader = makeUniqueNoThrow<FontDownloadActivity>(
      renderer, mappedInput, FontDownloadActivity::Purpose::ReaderAutoInstall, FontDownloadActivity::StartMode::Normal,
      globalSettingsSnapshotted_ ? &globalSettingsSnapshot_ : nullptr, &fontInstallLiveStyle_);
  if (!downloader) {
    LOG_ERR("FONT", "OOM allocating FontDownloadActivity (%zu bytes)", sizeof(FontDownloadActivity));
    return false;
  }
  startActivityForResult(std::move(downloader), [this](const ActivityResult&) {
    READING_STATS.resumeSession();
    requestUpdate();
  });
  return true;
}
#endif
void EpubReaderActivity::openFootnoteSelect(const bool reopenMenuOnCancel) {
  if (!section || currentPageFootnotes.empty()) return;
  if (currentPageFootnotes.size() == 1) {
    navigateToHref(currentPageFootnotes[0].href, true);
    return;
  }

  // The list borrows these entries; hold them stable until Push stops parent rendering.
  RenderLock lock;
  if (!section || currentPageFootnotes.empty()) return;
  std::unique_ptr<Activity> selector;
  if (mappedInput.hasTouch()) {
    auto page = section->loadPage(section->currentPage);
    if (!page) return;
    int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
    renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                     &orientedMarginLeft);
    orientedMarginTop += SETTINGS.screenMargin;
    orientedMarginLeft += SETTINGS.screenMargin;
    selector = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(renderer, mappedInput, std::move(page),
                                                                   orientedMarginLeft, orientedMarginTop);
  } else {
    selector = makeUniqueNoThrow<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes);
  }
  if (!selector) {
    LOG_ERR("ERA", "OOM: footnote selection");
    return;
  }
  startActivityForResult(std::move(selector), [this, reopenMenuOnCancel](const ActivityResult& result) {
    READING_STATS.resumeSession();
    if (result.isCancelled) {
      if (reopenMenuOnCancel) {
        openReaderMenu();
      } else {
        requestUpdate();
      }
      return;
    }
    const auto& footnoteResult = std::get<FootnoteResult>(result.data);
    navigateToHref(footnoteResult.href, true);
    requestUpdate();
  });
}

void EpubReaderActivity::invalidateClippingRender() {
  activityManager.cancelIdleRender();
  RenderLock lock;
#if FREEINK_DEVICE_READPICO
  ++renderEpoch_;
  freePageCache();
#endif
  discardOverlayPage();
  pageBufferStale = true;
}

void EpubReaderActivity::openClippings() {
  if (!CLIPPINGS.hasClippings()) {
    requestUpdate();
    return;
  }
  auto list = makeUniqueNoThrow<EpubReaderClippingListActivity>(renderer, mappedInput);
  if (!list) {
    LOG_ERR("CLIP", "OOM: clipping list");
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult& result) {
    invalidateClippingRender();
    READING_STATS.resumeSession();
    if (!result.isCancelled) {
      if (const auto* progress = std::get_if<ProgressChangeResult>(&result.data)) {
        applyProgressChange(*progress);
        return;
      }
    }
    requestUpdate();
  });
}

void EpubReaderActivity::loop() {
  if (loadFailurePopup.isActive()) {
    loadFailurePopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }
  if (!epub) {
    finish();
    return;
  }

  if (fontPromptState != FontPromptState::Idle) {
    lastPageTurnTime = millis();
    handleFontPreloadPrompt();
    return;
  }
  // Child activities suspend this loop; their temporary Overlay::None is not
  // a return to reading. Panel/tool switches retain the same preview session.
  if (overlay == Overlay::None && fontPreview.active()) {
    lastPageTurnTime = millis();
    finishFontPreview();
    return;
  }

#ifdef ENABLE_CHINESE_VERSION
  if (maybeOfferCompleteChineseFont()) return;
#endif

  READING_STATS.tickActiveSession();
  rememberBookOnceRendered();

  // The radio stopped for a chapter build: once the render task has let go of the lock, that page
  // is on screen; once the build tick below has finished the chapter, no parser is left alive and
  // the radio may start again.
  if (radioReleasedForBuild.load()) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && !(section && section->isBuilding())) {
      radioReleasedForBuild = false;
      bleturner::afterPaint();
    }
  }

  // The remote linked, or failed to, after the status bar showed its note: paint the page again.
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && linkNoteTitle.repaint(bleturner::linkNote())) requestUpdate();
  }

  // Someone else turned the screen while this reader was stacked (the control
  // center's orientation tile). Reflow before the next render, or the page
  // would be drawn with a layout built for the previous frame size.
  if (appliedOrientation != SETTINGS.orientation) {
    applyOrientation(SETTINGS.orientation);
    requestUpdate();
    return;
  }

#if !FREEINK_DEVICE_READPICO
  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  auto* fcm = renderer.getFontCacheManager();
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && section && !section->isBuilding() && renderer.hasFrameBuffer() &&
        lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
        ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC && fcm &&
        fcm->canIdlePrewarm(SETTINGS.getReaderFontId()) &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          const auto t0 = millis();
          auto scope = fcm->createPrewarmScope();
          p->render(renderer, SETTINGS.getReaderFontId(), 0, 0);
          scope.endScanAndPrewarm();
          LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
        }
      }
    }
  }

#endif

  if (!updateChapterBuild()) return;

  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();
  clearEndOfBookOptionsIfNeeded();

  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  // An option picker opened from a tap zone (e.g. the auto page-turn rate
  // dialog) owns all input while active, exactly like the one over the More
  // panel (which handleOverlayInput() gates when overlay != None). The
  // inherited contact is OptionPopup's own responsibility
  // (ignoreInitialTouchContact owns it through release so it cannot select or
  // dismiss the fresh dialog), so there is no timer here — a second tap right
  // after opening still has to work.
  if (overlay == Overlay::None && overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      requestUpdate();  // popup closed: repaint the clean page
    });
    return;
  }

  const auto touch =
      ReaderUtils::detectTouchPageTurn(renderer, mappedInput, ReaderUtils::isRtlBookLanguage(epub->getLanguage()));

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    requestUpdate();
  }

  if (showFontDamagedMessage && (millis() - fontDamagedMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showFontDamagedMessage = false;
    requestUpdate();
  }

  // The toolbar reader menu owns all input while shown, ahead of the automatic page turn
  // below: the More panel's rate popup switches automatic turning on and leaves the panel
  // open, so the timer must neither flip the page under it nor eat the panel's next
  // Confirm/Back release.
  if (overlay != Overlay::None) {
    if (usesToolbarMenu()) {
      // Hold the interval at zero elapsed so closing the panel starts a fresh one.
      lastPageTurnTime = millis();
      handleOverlayInput();
      return;
    }
    // The style was switched off while an overlay was up (Settings reached via
    // the More panel); fall back to the clean page.
    overlay = Overlay::None;
    discardOverlayPage();
    requestUpdate();
    return;
  }

  int selectionX = 0;
  int selectionY = 0;
  if (!atEndOfBook && SETTINGS.touchReaderControls && mappedInput.wasScreenLongPress(selectionX, selectionY)) {
    automaticPageTurnActive = false;
    pendingManualTurn = 0;
    startClipSelection(selectionX, selectionY);
    return;
  }

  switch (mappedInput.homeButtonAction()) {
    case HomeButtonAction::ReaderMenu:
    case HomeButtonAction::Bookmark:
    case HomeButtonAction::Sync:
    case HomeButtonAction::Dictionary:
    case HomeButtonAction::Footnotes:
    case HomeButtonAction::CreateClipping:
      automaticPageTurnActive = false;
      break;
    default:
      break;
  }

  if (automaticPageTurnActive) {
    const bool touchStopsAutoTurn = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) || touchStopsAutoTurn) {
      automaticPageTurnActive = false;
      if (touchStopsAutoTurn) haptic_feedback::touchAction();
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      const bool succeeded = pageTurn(true);
      notePageTurn(true, succeeded);
      requestUpdate();
      return;
    }
  }

  if (showClippingMessage && (millis() - clippingMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showClippingMessage = false;
    requestUpdate();
  }

  // While the end-of-book suggestion menu is up it owns Confirm/Back/navigation, so it
  // gets this tick's input first and the long-press shortcuts below stay inert behind it
  // -- a hold there must not drop a bookmark onto the suggestion screen or paint the
  // dictionary word picker over it. Anything the menu does not handle (long-press Back to
  // the file browser, say) still falls through to the regular handlers.
  if (handleEndOfBookMenu()) {
    return;
  }
  const bool endOfBookMenuOpen = endOfBookMenuActive();

  const unsigned long confirmHoldMs = confirmLongPressThreshold();
  // wasLongPressed() suppresses the release that follows it, so leave it unpolled while
  // the end-of-book menu owns Confirm -- otherwise the menu never sees that release.
  const bool confirmLongPressed = !endOfBookMenuOpen && confirmHoldMs != 0 &&
                                  mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, confirmHoldMs);
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (confirmLongPressed) {
    switch (SETTINGS.longPressMenuFunction) {
      case CrossPointSettings::LP_MENU_BOOKMARK:
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
        break;
      case CrossPointSettings::LP_MENU_KOSYNC:
#ifdef ENABLE_CHINESE_VERSION
        if (wereadBookId_[0] && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS && launchWeReadSync()) return;
#endif
        if (mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS && launchKOReaderSync()) return;
        break;
      case CrossPointSettings::LP_MENU_DICTIONARY:
        if (mappedInput.getHeldTime() >= ReaderUtils::BOOKMARK_HOLD_MS) {
          openDictionaryWordSelect();
          return;
        }
        break;
      case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
        startClipSelection();
        return;
      case CrossPointSettings::LP_MENU_FOOTNOTES:
        if (footnoteDepth > 0) {
          restoreSavedPosition();
        } else {
          openFootnoteSelect(false);
        }
        return;
      case CrossPointSettings::LP_MENU_READER_MENU:
      case CrossPointSettings::LP_MENU_DISABLED:
      default:
        break;
    }
  }

  if (!endOfBookMenuOpen) {
    switch (mappedInput.homeButtonAction()) {
      case HomeButtonAction::Bookmark:
        if (!showBookmarkMessage) {
          addBookmark();
          showBookmarkMessage = true;
          bookmarkMessageTime = millis();
          requestUpdate();
        }
        return;
      case HomeButtonAction::Sync:
#ifdef ENABLE_CHINESE_VERSION
        if (wereadBookId_[0]) {
          launchWeReadSync();
          return;
        }
#endif
        launchKOReaderSync();
        return;
      case HomeButtonAction::Dictionary:
        if (!showDictionaryMessage) openDictionaryWordSelect();
        return;
      case HomeButtonAction::CreateClipping:
        startClipSelection();
        return;
      case HomeButtonAction::ReaderMenu:
        if (usesToolbarMenu() && section)
          openOverlay(Overlay::Toolbar);
        else
          openReaderMenu();
        return;
      default:
        break;
    }
  }

  // Link taps take priority over the reader-menu and page-turn zones.
  if (!atEndOfBook && !currentPageLinks.empty() && SETTINGS.touchReaderControls && mappedInput.hasTouch()) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto* link = EpubReaderUtils::linkAtPoint(currentPageLinks, touchX, touchY, currentPageLinkMarginLeft,
                                                      currentPageLinkMarginTop);
      if (link) {
        navigateToHref(link->href, true);
        haptic_feedback::touchAction();
        return;
      }
    }
  }

  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (confirmReleased || touchMenu) {
    if (touchMenu) haptic_feedback::touchAction();
    // Toolbar style: the page is on screen and in the framebuffer, so paint the
    // toolbar over it (one refresh) instead of pushing a full-screen menu.
    if (usesToolbarMenu() && section) {
      pendingManualTurn = 0;
      openOverlay(Overlay::Toolbar);
    } else {
      openReaderMenu();
    }
    return;  // no build step before the menu shows
  }

  // Tap-zone short/long actions beyond the shared base set (bookmark,
  // dictionary, chapter, percent, KOReader, auto turn, menu). Long-press
  // bookmark/dictionary return false here so they keep flowing through the
  // dedicated hold/release handling below; everything else dispatches now,
  // ahead of the page-turn and footnote input.
  if (touch.longPress && touch.longAction != CrossPointSettings::TAP_ZONE_LONG_NONE &&
      handleZoneLongAction(touch.longAction)) {
    requestUpdate();
    return;
  }
  if (!touch.longPress && touch.action != CrossPointSettings::TAP_ZONE_NONE && handleZoneShortAction(touch.action)) {
    // A tap-zone option picker (auto page-turn) painted its own frame in
    // showAutoPageTurnPopup(); skipping the page re-render avoids the page
    // push flashing the fresh dialog away and back.
    if (!overlayPopup.isActive()) {
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && (footnoteDepth > 0 || isViewingNonlinearSpine()) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (handleBackNavigation()) {
    return;
  }

  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    startClipSelection();
    return;
  }

  if ((!endOfBookMenuOpen && mappedInput.homeButtonAction() == HomeButtonAction::Footnotes) ||
      (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
       mappedInput.wasReleased(MappedInputManager::Button::Power) &&
       !mappedInput.wasReleased(MappedInputManager::Button::Down))) {
    if (footnoteDepth > 0 || isViewingNonlinearSpine()) {
      restoreSavedPosition();
    } else {
      openFootnoteSelect(false);
    }
    return;
  }

  constexpr unsigned long kMinManualTurnGapMs = 200;
  const bool turnGuardActive = RenderLock::peek() || (millis() - lastPageTurnTime) < kMinManualTurnGapMs;
  if (pendingManualTurn != 0 && !turnGuardActive) {
    if (!section) {
      pendingManualTurn = 0;
      return;
    }
    const bool forward = pendingManualTurn > 0;
    pendingManualTurn = 0;
    const bool succeeded = pageTurn(forward);
    notePageTurn(forward, succeeded);
    if (succeeded && pendingManualTurnTouch) haptic_feedback::touchAction();
    pendingManualTurnTouch = false;
    requestUpdate();
    return;
  }

  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    // Nothing to act on in this frame: build more of the chapter.
    backgroundBuildTick();
    return;
  }

  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress = !fromTilt && heldMs >= ReaderUtils::SKIP_HOLD_MS;
  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
    const bool succeeded = skipPages(nextTriggered ? 1 : -1);
    notePageTurn(false, succeeded);
    if (succeeded && (touch.prev || touch.next)) haptic_feedback::touchAction(true);
    requestUpdate();
    return;
  }

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    if (touch.prev || touch.next) haptic_feedback::touchAction(true);
    requestUpdate();
    return;
  }

  if (!section) {
    requestUpdate();
    return;
  }

  if (turnGuardActive) {
    activityManager.cancelIdleRender();
    pendingManualTurn = prevTriggered ? -1 : 1;
    pendingManualTurnTouch = touch.prev || touch.next;
    return;
  }

  const bool succeeded = pageTurn(!prevTriggered);
  notePageTurn(!prevTriggered, succeeded);
  if (succeeded && (touch.prev || touch.next)) haptic_feedback::touchAction();
  requestUpdate();
}

bool EpubReaderActivity::handleZoneShortAction(const uint8_t action) {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_MENU:
      // The zone menu action mirrors the long-press path: it only works while
      // "Show reader menu" is set to tap. When the menu is bound to a swipe or
      // disabled, zone taps must fall through so the page turn logic stays
      // untouched (the picker also hides the menu option in that mode).
      if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
      if (usesToolbarMenu() && section) {
        openOverlay(Overlay::Toolbar);
      } else {
        openReaderMenu();
      }
      return true;
    case CrossPointSettings::TAP_ZONE_CHAPTER:
      // Jump straight to the next chapter through the same navigation the
      // toolbar uses. TXT/MD store their named chapters as anchors inside a
      // single spine, so stepping currentSpineIndex would clamp back to the
      // same chapter and the tap would look dead.
      if (gotoChapterIndex(currentChapterIndex() + 1)) requestUpdate();
      return true;
    case CrossPointSettings::TAP_ZONE_BOOKMARK:
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      return true;
    case CrossPointSettings::TAP_ZONE_DICTIONARY:
      openDictionaryWordSelect();
      return true;
    case CrossPointSettings::TAP_ZONE_FOOTNOTE:
      openFootnoteSelect(false);
      return true;
    case CrossPointSettings::TAP_ZONE_KOREADER:
      return launchKOReaderSync();
    case CrossPointSettings::TAP_ZONE_AUTO_TURN:
      showAutoPageTurnPopup();
      return true;
    case CrossPointSettings::TAP_ZONE_JUMP_PERCENT: {
      const int initialPercent = bookPercentFor(chapterPosition());
      startActivityForResultWith<EpubReaderPercentSelectionActivity>(
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
            requestUpdate();
          },
          initialPercent);
      return true;
    }
    default:
      return ReaderActivity::handleZoneShortAction(action);
  }
}

bool EpubReaderActivity::handleZoneLongAction(const uint8_t action) {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_LONG_BOOKMARK:
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_DICTIONARY:
      openDictionaryWordSelect();
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_CHAPTER:
      return handleZoneShortAction(CrossPointSettings::TAP_ZONE_CHAPTER);
    case CrossPointSettings::TAP_ZONE_LONG_FOOTNOTE:
      openFootnoteSelect(false);
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_MENU:
      if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
      if (usesToolbarMenu() && section) {
        openOverlay(Overlay::Toolbar);
      } else {
        openReaderMenu();
      }
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_KOREADER:
      return launchKOReaderSync();
    case CrossPointSettings::TAP_ZONE_LONG_AUTO_TURN:
      showAutoPageTurnPopup();
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_JUMP_PERCENT:
      return handleZoneShortAction(CrossPointSettings::TAP_ZONE_JUMP_PERCENT);
    default:
      return ReaderActivity::handleZoneLongAction(action);
  }
}

bool EpubReaderActivity::jumpToFraction(float fraction) {
  if (!epub || !std::isfinite(fraction)) return false;
  int targetSpineIndex = -1;
  float chapterFraction = 0.0f;
  if (!epub->findSpineForProgress(fraction, targetSpineIndex, chapterFraction)) return false;

  {
    RenderLock lock;
    clearDeferredReposition();
    leaveNonlinearSpine(targetSpineIndex);
    pendingAnchor.clear();
    pendingOffsetJump.reset();
    pendingPageJump.reset();
    pendingSpineProgress = chapterFraction;
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
  requestUpdate();
  return true;
}

void EpubReaderActivity::jumpToPercent(const int percent) {
  jumpToFraction(static_cast<float>(clampPercent(percent)) / 100.0f);
}

void EpubReaderActivity::applyProgressChange(const ProgressChangeResult& sync) {
  if (sync.hasVisibleTextOffset && sync.spineIndex >= 0 && sync.spineIndex < epub->getSpineItemsCount()) {
    RenderLock lock;
    if (epub->isLinearSpineItem(currentSpineIndex) && !epub->isLinearSpineItem(sync.spineIndex) &&
        !pushSavedPosition()) {
      return;
    }
    clearDeferredReposition();
    leaveNonlinearSpine(sync.spineIndex);
    const auto page = section && currentSpineIndex == sync.spineIndex
                          ? section->getPageForVisibleTextOffset(sync.visibleTextOffset)
                          : std::nullopt;
    if (page) {
      section->currentPage = *page;
    } else {
      currentSpineIndex = sync.spineIndex;
      pendingOffsetJump = sync.visibleTextOffset;
      nextPageNumber = std::max(0, sync.page);
      section.reset();
    }
    requestUpdate();
    return;
  }

  int targetSpineIndex = sync.spineIndex;
  int targetPage = sync.page;
  const int activeTotalPages = section ? section->estimatedTotalPages() : 0;
  const bool cachedPageMatchesActiveSection = section && sync.totalPages > 0 && currentSpineIndex == sync.spineIndex &&
                                              sync.page >= 0 && sync.page < sync.totalPages &&
                                              activeTotalPages == sync.totalPages;

  if (!cachedPageMatchesActiveSection && sync.hasSavedProgress) {
    const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
    CrossPointPosition fallback =
        ProgressMapper::toCrossPoint(epub, {sync.xpath, sync.percentage}, renderer, currentSpineIndex, totalPages);
    targetSpineIndex = fallback.spineIndex;
    targetPage = fallback.pageNumber;
  }

  RenderLock lock;
  if (epub->isLinearSpineItem(currentSpineIndex) && targetSpineIndex >= 0 &&
      targetSpineIndex < epub->getSpineItemsCount() && !epub->isLinearSpineItem(targetSpineIndex) &&
      !pushSavedPosition()) {
    return;
  }
  clearDeferredReposition();
  leaveNonlinearSpine(targetSpineIndex);

  if (currentSpineIndex != targetSpineIndex) {
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = targetPage;
    section.reset();
  } else if (section && section->currentPage != targetPage) {
    const int clampedTargetPage = std::max(0, targetPage);
    section->currentPage = clampedTargetPage;
  } else if (!section) {
    nextPageNumber = targetPage;
  }
  requestUpdate();
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    READING_STATS.resumeSession();
    loadCachedBookmarks();
    invalidateClippingRender();
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      applyProgressChange(sync);
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const int tocIdx = currentTocIndex();
      // Release the section while the chapter list is up (mirrors the
      // TEXT_SETTINGS path): picking a chapter resets it anyway, and its
      // tens-of-KB footprint is the difference between the chapter list
      // holding its CJK glyph arena (RAM-only repaints) and re-reading
      // glyphs from SD on every row step. Cancel restores via the same
      // cached-position rebuild TEXT_SETTINGS uses.
      {
        RenderLock lock;
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResultWith<EpubReaderChapterSelectionActivity>(
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            clearDeferredReposition();
            leaveNonlinearSpine(chapterResult.spineIndex);
            currentSpineIndex = chapterResult.spineIndex;
            pendingAnchor = chapterResult.anchor;
            nextPageNumber = 0;
            section.reset();
            requestUpdate();
          },
          epub, spineIdx, tocIdx);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      openFootnoteSelect(true);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResultWith<TextSettingsActivity>(
          [this](const ActivityResult&) {
            READING_STATS.resumeSession();
            if (persistBookStyleEdits()) {
              // Book override mode: the edits are now this book's overrides and
              // the settings file is back on the entered global snapshot.
              // Reflow with the overrides without persisting them to the global
              // settings file: per-book overrides live in the book-style store
              // only. ensureLoaded() frees and replaces the resident SD font,
              // so it must hold the render lock like
              // applyReaderTextSettings() does.
              RenderLock lock;
              sdFontSystem.ensureLoaded(renderer);
              if (section) {
                rememberCurrentContentOffset();
                cachedSpineIndex = currentSpineIndex;
                cachedChapterTotalPageCount = section->pageCount;
                nextPageNumber = section->currentPage;
              }
              section.reset();
            } else {
              // Per-book memory off: the edits are global and persist as-is.
              applyReaderTextSettings();
            }
            openReaderMenu();
          },
          &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
          TextSettingsActivity::InitialFontState::Unchanged, TextSettingsActivity::StartMode::Interactive, &bookPath,
          globalSettingsSnapshotted_ ? &globalSettingsSnapshot_ : nullptr);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH:
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
      // Handled in-place by EpubReaderMenuActivity so its On/Off value updates
      // without closing the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in-place by EpubReaderMenuActivity using the live frontlight HAL.
      break;
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      // Both open option popups inside EpubReaderMenuActivity and only reach
      // the result callback after another action closes the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const int initialPercent = bookPercentFor(chapterPosition());
      startActivityForResultWith<EpubReaderPercentSelectionActivity>(
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          },
          initialPercent);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResultWith<QrDisplayActivity>(
              [this](const ActivityResult&) {
                READING_STATS.resumeSession();
                openReaderMenu();
              },
              fullText);
          break;
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
#ifdef ENABLE_CHINESE_VERSION
      if (wereadBookId_[0]) {
        launchWeReadSync();
        break;
      }
#endif
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResultWith<EpubReaderBookmarksActivity>(progressChangeResultHandler, epub, epub->getPath());
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING: {
      startClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS: {
      openClippings();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      break;
    }
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  // The setting is hidden on these boards, so a stale stored value must not act.
  if (BoardConfig::hasTouch()) return 0;
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
    case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      return ReaderUtils::BOOKMARK_HOLD_MS;
    case CrossPointSettings::LP_MENU_KOSYNC:
#ifdef ENABLE_CHINESE_VERSION
      if (wereadBookId_[0]) return ReaderUtils::GO_HOME_MS;
#endif
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  flushReaderSession();

  // Without server credentials the same save/release/network flow drains only
  // queued plugin events. A failed save never launches either network action.
  const bool pluginEventsOnly = !KOREADER_STORE.hasCredentials();

  RenderLock renderLock;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  CrossPointPosition localPos = getReadingPosition();
  std::string localChapterName = getStatsChapterTitle(
      *epub, localPos.spineIndex,
      localPos.hasVisibleTextOffset ? std::optional<uint32_t>(localPos.visibleTextOffset) : std::nullopt);
  const std::string savedEpubPath = epub->getPath();

  // Allocate the one future owner before releasing reader state. Its constructor
  // only stores metadata/UI state; WiFi starts in onEnter after replacement.
  const size_t ownerBytes = sizeof(KOReaderSyncActivity) + savedEpubPath.size() + 1;
  if (!memory::hasAllocationHeadroom(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ownerBytes,
                                     std::max(sizeof(KOReaderSyncActivity), savedEpubPath.size() + 1), 32 * 1024,
                                     4096)) {
    LOG_ERR("KOSync", "Low heap for sync activity");
    return false;
  }
  auto syncActivity =
      makeUniqueNoThrow<KOReaderSyncActivity>(renderer, mappedInput, savedEpubPath, localPos, SavedProgressPosition{},
                                              std::move(localChapterName), pluginEventsOnly);
  if (!syncActivity) {
    LOG_ERR("KOSync", "OOM: sync activity (%u bytes)", static_cast<unsigned>(sizeof(KOReaderSyncActivity)));
    return false;
  }

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    if (section) {
      nextPageNumber = section->currentPage;
    }
    discardOverlayPage();
    ImageBlock::releaseRenderCache();
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseSdFontCaches();
    }
    // No rendering may run while the chapter mapper borrows the framebuffer.
    if (!pluginEventsOnly) {
      GfxRenderer::FrameBufferLoan loan(renderer);
      syncActivity->setLocalMappedProgress(ProgressMapper::toSavedProgress(epub, localPos));
    }
    // The destructor can no longer save the back-stack once epub is gone.
    if (footnoteDepth > 0 && !isViewingNonlinearSpine()) saveLinkStack();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::move(syncActivity));
  return true;
}

#ifdef ENABLE_CHINESE_VERSION
bool EpubReaderActivity::launchWeReadSync() {
  if (!wereadBookId_[0]) return false;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const CrossPointPosition localPosition = getReadingPosition();
  const float chapterFraction = localPosition.totalPages > 1
                                    ? static_cast<float>(localPosition.pageNumber) / (localPosition.totalPages - 1)
                                    : 0.0f;
  const float localFraction = epub->calculateProgress(localPosition.spineIndex, chapterFraction);
  std::string savedEpubPath = epub->getPath();

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("WRSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  const auto context = WeReadProgressSyncActivity::makeContext(*epub, wereadBookId_, localFraction, localPosition);
  auto sync = makeUniqueNoThrow<WeReadProgressSyncActivity>(renderer, mappedInput, std::move(savedEpubPath),
                                                            wereadBookId_, context);
  if (!sync) {
    LOG_ERR("WRSync", "OOM: WeReadProgressSyncActivity (%u bytes)",
            static_cast<unsigned>(sizeof(WeReadProgressSyncActivity)));
    pendingSyncLaunchError = true;
    requestUpdate();
    return true;
  }

  {
    RenderLock lock;
    if (section) nextPageNumber = section->currentPage;
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    epub.reset();
  }
  activityManager.replaceActivity(std::move(sync));
  return true;
}
#endif

void EpubReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one — that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    // Reflow is a global change, but with per-book memory on SETTINGS also
    // carries this book's typography overrides; persist the entered global
    // values so they never reach the settings file.
    saveSettingsKeepingBookStyle();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t requestedPageTurnRate) {
  if (requestedPageTurnRate == 0 || requestedPageTurnRate > MAX_PAGE_TURN_RATE) {
    automaticPageTurnActive = false;
    return;
  }

  pageTurnRate = requestedPageTurnRate;
  lastPageTurnTime = millis();
  pageTurnDuration = (1UL * 60 * 1000) / pageTurnRate;
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

bool EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (!section) return false;
  READING_STATS.noteActivity();
  // A "Bluetooth failed" note has been read: the page this turn paints shows the title again.
  bleturner::acknowledgeLinkNote();
  {
    RenderLock lock;
    clearDeferredReposition();
  }
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1 || section->isBuilding()) {
      section->currentPage++;
      lastPageTurnTime = millis();
      return true;
    } else if (isViewingNonlinearSpine()) {
      restoreSavedPosition();
      lastPageTurnTime = millis();
      return true;
    } else if (const int next = epub->getNextLinearSpineIndex(currentSpineIndex); next >= 0) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex = next;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    } else {
      currentSpineIndex = epub->getSpineItemsCount();
      lastPageTurnTime = millis();
      return true;
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
      lastPageTurnTime = millis();
      return true;
    } else if (isViewingNonlinearSpine()) {
      restoreSavedPosition();
      lastPageTurnTime = millis();
      return true;
    } else if (const int previous = epub->getPreviousLinearSpineIndex(currentSpineIndex); previous >= 0) {
      RenderLock lock;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex = previous;
      section.reset();
      lastPageTurnTime = millis();
      return true;
    }
  }
  return false;
}

bool EpubReaderActivity::skipPages(int amount) {
  if (!section) return false;
  READING_STATS.noteActivity();
  if (isViewingNonlinearSpine()) {
    restoreSavedPosition();
    return true;
  }
  bleturner::acknowledgeLinkNote();
  if (amount > 0) {
    RenderLock lock;
    nextPageNumber = 0;
    const int next = epub->getNextLinearSpineIndex(currentSpineIndex);
    currentSpineIndex = next >= 0 ? next : epub->getSpineItemsCount();
    section.reset();
    return true;
  } else {
    if (section->currentPage > 0) {
      section->currentPage = 0;
      return true;
    } else if (const int previous = epub->getPreviousLinearSpineIndex(currentSpineIndex); previous >= 0) {
      RenderLock lock;
      nextPageNumber = 0;
      currentSpineIndex = previous;
      section.reset();
      return true;
    }
  }
  return false;
}

// Failed protected open: show the standard option dialog (wrapped message)
// instead of silently falling back to the previous screen. Exact error
// strings are set by openProtectedBook (ContentProtection.cpp).
bool EpubReaderActivity::handleLoadFailure() {
  if (loadProtectionError.empty()) return false;
  const std::string& perr = loadProtectionError;
  StrId msg = StrId::STR_DRM_PROTECTED_FILE;
  bool offerSync = false;
  if (perr == "txt progress migration failed") {
    msg = StrId::STR_TXT_PROGRESS_MIGRATION_FAILED;
  } else if (perr == "access expired") {
    msg = StrId::STR_LOAN_EXPIRED;
  } else if (perr == "loan date unverified") {
    msg = StrId::STR_LOAN_TIME_UNVERIFIED;
    offerSync = true;
  }
  const char* options[2] = {I18N.get(offerSync ? StrId::STR_CLOCK_SYNC_NOW : StrId::STR_OK_BUTTON),
                            I18N.get(StrId::STR_OK_BUTTON)};
  loadFailurePopup.showMessage("", I18N.get(msg), options, offerSync ? 2 : 1, 0, [this, offerSync](const int index) {
    if (offerSync && index == 0) {
      beginLoanTimeSync();
      return;
    }
    finish();
  });
  requestUpdate();
  return true;  // stay alive; the popup's Back dismiss lands in loop()'s !epub finish
}

void EpubReaderActivity::beginLoanTimeSync() {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("ERS", "OOM: Wi-Fi selection for loan time sync");
    finish();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
      finish();
      return;
    }
    GUI.drawPopup(renderer, tr(STR_SYNCING_TIME));
    const bool synced = trustedtime::syncNow(5000);
    WiFi.disconnect(false);
    delay(30);
    if (!synced) {
      // Reopening would hit the same unverified-time refusal: offer a retry.
      const char* options[2] = {I18N.get(StrId::STR_RETRY), I18N.get(StrId::STR_OK_BUTTON)};
      loadFailurePopup.showMessage("", I18N.get(StrId::STR_CLOCK_SYNC_FAIL), options, 2, 0, [this](const int index) {
        if (index == 0) {
          beginLoanTimeSync();
          return;
        }
        finish();
      });
      requestUpdate();
      return;
    }
    APP_STATE.openEpubPath = bookPath;
    APP_STATE.saveToFile();
    // Reboot straight back into this book with a clean heap (no-op on touch
    // boards, which fall through to the in-place relaunch below).
    silentRestartToReader();
    activityManager.goToReader(bookPath);
  });
}

bool EpubReaderActivity::remoteShortcut(const bool readerMenu) {
  if (!section) return false;
  automaticPageTurnActive = false;
  if (!readerMenu)
    startClipSelection();
  else if (usesToolbarMenu())
    openOverlay(Overlay::Toolbar);
  else
    openReaderMenu();
  return true;
}

bool EpubReaderActivity::isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }

void EpubReaderActivity::onReturnFromEndOfBook() {
  if (epub && epub->getLinearSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineIndexForLinearIndex(epub->getLinearSpineItemsCount() - 1);
    nextPageNumber = 0;
    pendingPageJump = std::numeric_limits<uint16_t>::max();
  }
}

// A started build runs to the end of the chapter, one bounded step per idle loop frame, so the
// parser and its page LUT are freed soon (and a radio stopped for the build comes back with no
// parser alive). Not while the radio holds the heap, and not in a spine whose background build
// failed.
bool EpubReaderActivity::backgroundBuildWanted() const {
  const auto radio = bleturner::status();
#if CONFIG_IDF_TARGET_ESP32C3 || CROSSPOINT_BLE_HOST_PSRAM
  const bool room = !radio.starting && !radio.stopping;
#else
  const bool room = !bleturner::holdsHeap();
#endif
  return section && section->isBuilding() && room && currentSpineIndex != backgroundBuildFailedSpine;
}

void EpubReaderActivity::render(RenderLock&& lock) {
  if (loadFailurePopup.isActive()) {
    renderer.clearScreen();
    loadFailurePopup.processRender(renderer, mappedInput);
    return;
  }
  if (fontPromptState == FontPromptState::TooLarge) {
    fontpreload::drawTooLargeNotice(renderer);
    renderer.displayBuffer();
    return;
  }
  ReaderActivity::render(std::move(lock));
  // Rebuild the page underneath, including after a global control-center visit.
  if (fontPromptState != FontPromptState::Idle) {
    overlayPopup.processRender(renderer, mappedInput);
  }
}

bool EpubReaderActivity::skipLoopDelay() {
  // The main loop holds the render lock while querying this hint. It keeps yielding while a button
  // is down: a press that starts inside a long build step gets its second debounce sample ~5 ms
  // after the step instead of after a 10 ms sleep.
  return overlay == Overlay::None && !buildHeapPaused && backgroundBuildWanted();
}

void EpubReaderActivity::renderBook() {
  currentPageLinks.clear();
  if (!epub) return;
  // A radio start in flight takes ~50 KB while the stack comes up: the page waits for it rather than
  // take the heap beside it (X3: abort() in a page restore at free 22,676 B). About 20 ms, once a visit.
  bleturner::settleStart();
  // Runs under the render task's RenderLock; catches every requestUpdate()
  // exit from the overlay while its deferred chrome refresh is still pending.
  settleOverlayRefresh();

  // Apply the reader's image-resampling choice before anything is drawn. The
  // setting can change from the reader menu, which does NOT reload the book, so
  // syncing only in loadBook() left the previous filter in force for the rest of
  // the session — the menu looked like it did nothing. The setter is a no-op
  // when the filter has not changed, so this costs a compare per render.
  ImageBlock::setBilinearScaling(SETTINGS.imageScaling == CrossPointSettings::IMAGE_SCALING_BILINEAR);

  const auto showPendingSyncSaveError = [this]() {
    if (pendingSyncSaveError) {
      pendingSyncSaveError = false;
      GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
    } else if (pendingSyncLaunchError) {
      pendingSyncLaunchError = false;
      GUI.drawPopup(renderer, tr(STR_SYNC_FAILED_MSG));
    }
  };

  const auto showBuildError = [this](const char* message = nullptr) {
    renderer.clearScreen();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    GUI.drawPopup(renderer, message ? message : tr(STR_INDEX_FAILED));
    automaticPageTurnActive = false;
  };

  if (failedBuildSpine_ == currentSpineIndex) {
    showBuildError();
    return;
  }
  failedBuildSpine_ = -1;

  if (currentSpineIndex < 0) currentSpineIndex = 0;
  if (currentSpineIndex > epub->getSpineItemsCount()) currentSpineIndex = epub->getSpineItemsCount();

  if (currentSpineIndex == epub->getSpineItemsCount()) {
    return;
  }

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

  int bottomReserve = statusBarHeight;
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    bottomReserve += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
  }
  if (UiHighDpiProfile::enabled && (SETTINGS.statusBarSpec().textLaneVisible() || automaticPageTurnActive)) {
    // Reuse the footer's unused top space while keeping a gap above its text.
    bottomReserve -=
        std::max(0, UITheme::getStatusBarTextTopPadding(renderer) - UiHighDpiProfile::readerContentStatusGap);
  }
  orientedMarginBottom += std::max(static_cast<int>(SETTINGS.screenMargin), bottomReserve);

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = effectiveRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = makeUniqueNoThrow<Section>(epub, currentSpineIndex, renderer);
    if (!section) {
      LOG_ERR("ERS", "OOM: Section (%u bytes)", static_cast<unsigned>(sizeof(Section)));
      if (handleBuildFailure("section allocation", Section::BuildError::OutOfMemory)) return;
      showBuildError();
      return;
    }

#if FREEINK_DEVICE_READPICO
    ++sectionGeneration_;
#endif
    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      cachedChapterTotalPageCount = 0;
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
    if (Txt::isTxtOrMd(epub->getPath()) && !pendingAnchor.empty()) {
      uint32_t visibleOffset = 0;
      if (!Txt::resolveChapterPosition(epub->getPath(), epub->getCachePath(), pendingAnchor, visibleOffset)) {
        LOG_ERR("ERS", "Failed to resolve TXT chapter %s", pendingAnchor.c_str());
        showBuildError();
        return;
      }
      pendingOffsetJump = visibleOffset;
      pendingAnchor.clear();
    }
    const bool explicitOffsetJump = pendingOffsetJump.has_value();
    const std::optional<uint32_t> offsetJump =
        explicitOffsetJump ? pendingOffsetJump
        : (pendingPageJump.has_value() || !pendingAnchor.empty() || currentSpineIndex != cachedSpineIndex)
            ? std::nullopt
            : cachedVisibleTextOffset;
    if (!cacheComplete) {
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      const bool needsFullBuild = pendingPercentJump;
      if (needsFullBuild) {
        prepareChapterBuild();
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        pagesUntilFullRefresh = 1;
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
        };
        if (!makeRoomForChapterBuild()) {
          section.reset();
          showBuildError(tr(STR_BT_BUILD_REFUSED));
          return;
        }
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          loan.end();
          if (handleBuildFailure("full build", section->buildError())) return;
          showBuildError();
          return;
        }
        loan.end();
      } else {
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        if (section->isPartial() &&
            (anchorJump ? section->getPageForAnchor(pendingAnchor).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          const bool willInflate = !section->hasHtmlCache();
          bool showPopup;
          if (anchorJump) {
            showPopup = !section->findAnchor(pendingAnchor).has_value() && spineBytes > BUILD_POPUP_BYTE_THRESHOLD;
          } else {
            const bool targetAvailable = target < static_cast<int>(section->pageCount);
            showPopup = !targetAvailable && ((spineBytes > BUILD_POPUP_BYTE_THRESHOLD && willInflate) ||
                                             target > BUILD_POPUP_PAGE_THRESHOLD);
          }
          if (showPopup) {
            GUI.drawPopup(renderer, tr(STR_INDEXING));
            pagesUntilFullRefresh = 1;
          }
          buildPopupPending = !showPopup;
          // Section (re)builds are the heap-hungriest path (per-word
          // allocations for the whole section). Under TTF heap pressure, shed
          // every rebuildable font cache first — dropped glyphs re-fault on
          // demand after the build. Skipped for cpfont/builtin reading: the
          // release drops the persistent advance table and mini tables, which
          // would force SD metric re-reads on every fresh chapter for no gain.
          if (!renderer.getTtfFonts().empty()) {
            if (auto* fcm = renderer.getFontCacheManager()) {
              fcm->releaseSdFontCaches();
            }
          }
          if (!makeRoomForChapterBuild()) {
            section.reset();
            buildPopupPending = false;
            showBuildError(tr(STR_BT_BUILD_REFUSED));
            return;
          }
          LOG_DBG("ERS", "Heap before section build: %u (max block %u)", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
          const unsigned long buildStartMs = millis();
          bool started;
          {
            prepareChapterBuild();
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(renderer, pagesUntilFullRefresh); });
          }
          if (!started) {
            if (handleBuildFailure("start", section->buildError())) return;
            showBuildError();
            return;
          }
          while (!section->isBuildComplete() &&
                 (anchorJump               ? !section->findAnchor(pendingAnchor)
                  : offsetJump.has_value() ? !section->buildReachedVisibleTextOffset(*offsetJump)
                                           : static_cast<int>(section->pageCount) <= target)) {
            if (buildPopupPending && millis() - buildStartMs >= BUILD_POPUP_DEADLINE_MS) {
              showBuildPopup(renderer, pagesUntilFullRefresh);
            }
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              if (handleBuildFailure("foreground build", section->buildError())) return;
              showBuildError();
              return;
            }
          }
          buildPopupPending = false;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) section->currentPage = 0;
    }

    if (offsetJump.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*offsetJump)) {
        section->currentPage = *offsetPage;
        legacyProgressPending = false;
        clearDeferredReposition();
      }
    }
    if (legacyProgressPending) {
      loadProtectionError = "txt progress migration failed";
      handleLoadFailure();
      return;
    }
    if (explicitOffsetJump) {
      clearDeferredReposition();
    }
    pendingOffsetJump.reset();

    if (!pendingAnchor.empty()) {
      const auto page = section->findAnchor(pendingAnchor);
      if (page) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) newPage = section->pageCount - 1;
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
    if (stylesDisabledForSession_) {
      LOG_INF("ERS", "Basic layout ready: spine=%d page=%d (free=%u, min=%u, maxAlloc=%u)", currentSpineIndex,
              section->currentPage, static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
  }

  // The two loops below build on until the page to show exists.
  if ((section->isPartial() || section->isBuilding()) && section->currentPage >= static_cast<int>(section->pageCount) &&
      !makeRoomForChapterBuild()) {
    section.reset();
    showBuildError(tr(STR_BT_BUILD_REFUSED));
    return;
  }
  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 1;
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    if (!section->isBuilding()) prepareChapterBuild();
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      if (handleBuildFailure("partial start", section->buildError())) return;
      showBuildError();
      return;
    }
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        if (handleBuildFailure("extension build", section->buildError())) return;
        showBuildError();
        return;
      }
    }
  }
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        if (handleBuildFailure("extension build", section->buildError())) return;
        showBuildError();
        return;
      }
    }
  }

  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  applyDeferredReposition();

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    pageBufferStale = false;
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  updateBookmarkFlag();

  {
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      automaticPageTurnActive = false;
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        showPendingSyncSaveError();
        return;
      }
      requestUpdate();
      showPendingSyncSaveError();
      return;
    }
    pageLoadRetryCount = 0;

    currentPageVisibleOffset = p->visibleTextOffset;
    currentPageFootnotes = std::move(p->footnotes);
    currentPageLinks = std::move(p->links);
    currentPageLinkMarginLeft = orientedMarginLeft;
    currentPageLinkMarginTop = orientedMarginTop;

    // The overlay and non-tiled grayscale renderer share the renderer's single
    // stored-BW slot. Release the old page snapshot before renderContents()
    // needs that slot, then snapshot the newly rendered page below.
    discardOverlayPage();

    const auto start = millis();
    if (!renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom,
                        orientedMarginLeft)) {
      LOG_ERR("ERS", "Page cancelled or display commit failed; preserving saved progress");
      return;
    }
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    renderedSpineIndex_ = currentSpineIndex;
    committedPageNumber_ = section->currentPage;
    lastRenderCompleteMs = millis();
    markPageRendered();
  }

  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages())) {
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->estimatedTotalPages();
    }
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }

  if (showBookmarkMessage) {
    GUI.drawPopup(renderer, bookmarkRemoved ? tr(STR_BOOKMARK_REMOVED) : tr(STR_BOOKMARK_ADDED));
  }

  if (showDictionaryMessage) {
    GUI.drawPopup(renderer, tr(STR_DICT_NO_DICT_SET));
  }

  // Glyphs of a damaged .cpfont draw as blanks; say why, once per loaded font.
  if (SdCardFont::takeCorruptGlyphNotice()) {
    showFontDamagedMessage = true;
    fontDamagedMessageTime = millis();
  }
  if (showFontDamagedMessage) {
    GUI.drawPopup(renderer, tr(STR_FONT_DAMAGED));
  }

  if (showClippingMessage) {
    GUI.drawPopup(renderer, I18N.get(clippingMessage));
  }

  pageBufferStale = false;  // the page is back in the framebuffer

  // Toolbar menu: overlay the toolbar / panel on top of the freshly rendered page.
  if (overlay != Overlay::None && usesToolbarMenu()) {
    // The page just re-rendered under the overlay: refresh the snapshot that
    // backs panel->toolbar restores (any previous copy is stale).
    overlayPageStored = renderer.storeBwBuffer();
    renderOverlay();
    // An open option picker rides on top of the freshly drawn panel.
    if (overlayPopup.isActive()) overlayPopup.render(renderer);
    // FAST, same as openOverlay: HALF's inverting pass flashes the sheet
    // (white, in night mode) on every repaint under an open panel. Any AA
    // residue a FAST differential leaves under the chrome has not shown in
    // practice; restore a HALF cleanup here if text ever visibly ghosts
    // through the sheet (see #2190 for the mechanism).
    pushOverlayRefresh();
  }

  // An option picker opened directly over the reading page (tap-zone actions
  // such as auto page-turn) rides on top of the freshly rendered page. The
  // toolbar-menu popup is drawn together with its panel above; repaint this one
  // after the page push so a page re-render cannot wipe it off screen.
  if (overlay == Overlay::None && overlayPopup.isActive()) {
    overlayPopup.render(renderer);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void EpubReaderActivity::onEndOfBookRendered() {
  automaticPageTurnActive = false;
  if (pendingSyncSaveError) {
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  } else if (pendingSyncLaunchError) {
    pendingSyncLaunchError = false;
    GUI.drawPopup(renderer, tr(STR_SYNC_FAILED_MSG));
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  if ((!cachedVisibleTextOffset.has_value() && cachedChapterTotalPageCount == 0) || !section || section->isBuilding()) {
    return false;
  }
  bool changed = false;
  if (currentSpineIndex == cachedSpineIndex) {
    int newPage = section->currentPage;
    bool mappedOffset = false;
    if (cachedVisibleTextOffset.has_value()) {
      if (const auto offsetPage = section->getPageForVisibleTextOffset(*cachedVisibleTextOffset)) {
        newPage = *offsetPage;
        mappedOffset = true;
      }
    }
    if (!mappedOffset && cachedChapterTotalPageCount > 0 && section->pageCount != cachedChapterTotalPageCount) {
      const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
      newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    }
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  clearDeferredReposition();
  return changed;
}

void EpubReaderActivity::clearDeferredReposition() {
  cachedChapterTotalPageCount = 0;
  cachedVisibleTextOffset.reset();
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  if (legacyProgressPending) return false;
  const bool savedOrigin =
      footnoteDepth > 0 && savedPositions[0].spineIndex == spineIndex && savedPositions[0].pageNumber == currentPage;
  if (!savedOrigin && (spineIndex != renderedSpineIndex_ || currentPage != committedPageNumber_)) return false;
  int progressPercent = 0;
  const bool nonlinear = isViewingNonlinearSpine();
  const auto readingPosition =
      nonlinear ? getReadingPosition() : CrossPointPosition{spineIndex, currentPage, pageCount};
  if (nonlinear) {
    spineIndex = readingPosition.spineIndex;
    currentPage = readingPosition.pageNumber;
    pageCount = readingPosition.totalPages;
    if (spineIndex < 0) return false;
  }
  if (epub->getBookSize() > 0 && pageCount > 0) {
    const float chapterProgress = static_cast<float>(currentPage + 1) / static_cast<float>(pageCount);
    progressPercent =
        clampPercent(static_cast<int>(epub->calculateProgress(spineIndex, chapterProgress) * 100.0f + 0.5f));
  }

  std::optional<uint32_t> offset;
  if (nonlinear && readingPosition.hasVisibleTextOffset) {
    offset = readingPosition.visibleTextOffset;
  } else if (footnoteDepth > 0 && savedPositions[0].spineIndex == spineIndex &&
             savedPositions[0].pageNumber == currentPage) {
    offset = savedPositions[0].visibleTextOffset;
  } else if (section && spineIndex == currentSpineIndex && currentPage >= 0 && currentPage < section->pageCount) {
    offset = (currentPage == section->currentPage && currentPageVisibleOffset.has_value())
                 ? currentPageVisibleOffset
                 : section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage));
  }
  uint8_t chapterProgress = getStatsChapterProgressPercent(currentPage, pageCount);
  const int tocIndex = currentTocIndex();
  const std::string chapterTitle = spineIndex == currentSpineIndex && !Txt::isTxtOrMd(epub->getPath()) && tocIndex >= 0
                                       ? epub->getTocItem(tocIndex).title
                                       : getStatsChapterTitle(*epub, spineIndex, offset, &chapterProgress);
  READING_STATS.updateProgress(static_cast<uint8_t>(progressPercent), progressPercent >= 100, chapterTitle,
                               chapterProgress);
  const bool saved = EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, offset);
#ifdef ENABLE_CHINESE_VERSION
  if (saved && clearInitialProgressAfterSave_ && WeReadStore::clearInitialProgress(wereadBookId_)) {
    clearInitialProgressAfterSave_ = false;
  }
#endif
  return saved;
}

void EpubReaderActivity::rememberCurrentContentOffset() {
  cachedVisibleTextOffset.reset();
  if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
    cachedVisibleTextOffset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(section->currentPage));
  }
}

#if FREEINK_DEVICE_READPICO
bool EpubReaderActivity::pageCacheEligible() const {
  // No !section->isBuilding() here, deliberately. The page cache renders ONE page --
  // the current one or the next -- and Section::loadPage() is written to serve exactly
  // that while a build is in flight: it reads from the in-RAM build LUT, and returns
  // null for a page the build has not laid out yet. The caller already treats that null
  // as a clean skip. Gating the whole feature on the chapter being finished coupled two
  // independent things, and on this board the chapter build routinely does not finish:
  // the CSS parse needs 65,536 B and the device has ~48 KB free while reading, so the
  // build stalls (the log stops at "Page 8 processed" and never reports completion),
  // isBuilding() stays true for the rest of the session, and the cache then never
  // engages at all -- every turn is a full render instead of the cached one.
  return !pageCacheFailed_ && section && renderer.hasFrameBuffer() && !activityManager.isSwitchPending() &&
         overlay == Overlay::None && fontPromptState == FontPromptState::Idle && !renderer.isInverted() &&
         SETTINGS.textAntiAliasing && !SETTINGS.readingBackgroundEnabled && !renderer.supportsStripGrayscale() &&
         renderer.supportsTextOnlyCombinedBase();
}

ReaderPageCacheKey EpubReaderActivity::pageCacheKey(const int page, const int top, const int right, const int bottom,
                                                    const int left) const {
  return {
      .spec = effectiveRenderSpec(renderer.getScreenWidth() - left - right, renderer.getScreenHeight() - top - bottom),
      .sectionGeneration = sectionGeneration_,
      .renderEpoch = renderEpoch_,
      .spine = currentSpineIndex,
      .page = page,
      .top = top,
      .right = right,
      .bottom = bottom,
      .left = left,
      .orientation = SETTINGS.orientation,
      .fakeBold = SETTINGS.fakeBold,
      .antiAliasing = SETTINGS.textAntiAliasing != 0,
      .inverted = renderer.isInverted(),
      .background = SETTINGS.readingBackgroundEnabled != 0,
      .guideLine = SETTINGS.readingGuideLineEnabled != 0,
      .guideStyle = SETTINGS.readingGuideLineStyle,
      .guideOffset = SETTINGS.readingGuideLineOffset};
}

uint32_t EpubReaderActivity::idleRenderDelayMs() const { return pageCacheFailed_ ? 0 : 400; }

void EpubReaderActivity::freePageCache() {
  for (int slot = 0; slot < kPageCacheSlots; ++slot) {
    pageCacheBase_[slot].reset();
    pageCacheLsb_[slot].reset();
    pageCacheMsb_[slot].reset();
    pageCacheStash_[slot].reset();
    pageCache_[slot].state = ReaderPageCache::State::Empty;
  }
}

void EpubReaderActivity::renderIdle(const uint32_t generation) {
  const auto cancelled = [&] { return activityManager.idleRenderCancelled(generation); };
  // Every exit below names the condition it took. Without that, "the cache never hits"
  // is indistinguishable from "the build was cancelled", "the layout moved since the
  // last render" and "the next page has images" -- and each of those needs a different
  // fix. The lines are cheap: this hook runs at most once between two renders.
  if (cancelled()) {
    LOG_DBG("ERS", "Page cache: skip, cancelled before start");
    return;
  }
  if (!pageCacheEligible()) {
    // Name every input to pageCacheEligible(), because "not eligible" is ten conditions
    // at once and only one of them is the one that fires. The display-capability pair at
    // the end is the reason this needs saying out loud: on readpico the panel is driven
    // row by row from an LCD_CAM path, so supportsStripGrayscale() and
    // supportsTextOnlyCombinedBase() can legitimately differ from the SSD1677 boards the
    // gate was written against.
    LOG_DBG("ERS",
            "Page cache: skip, not eligible (failed=%d section=%d building=%d fb=%d switch=%d ovl=%d prompt=%d "
            "inv=%d aa=%d bg=%d strip=%d combined=%d)",
            static_cast<int>(pageCacheFailed_), static_cast<int>(section != nullptr),
            static_cast<int>(section != nullptr && section->isBuilding()), static_cast<int>(renderer.hasFrameBuffer()),
            static_cast<int>(activityManager.isSwitchPending()), static_cast<int>(overlay != Overlay::None),
            static_cast<int>(fontPromptState != FontPromptState::Idle), static_cast<int>(renderer.isInverted()),
            static_cast<int>(SETTINGS.textAntiAliasing), static_cast<int>(SETTINGS.readingBackgroundEnabled),
            static_cast<int>(renderer.supportsStripGrayscale()),
            static_cast<int>(renderer.supportsTextOnlyCombinedBase()));
    return;
  }
  const auto& layout = renderedPageKey_;
  // A main-loop reflow/jump may have replaced the section since the last render.
  if (layout != pageCacheKey(section->currentPage, layout.top, layout.right, layout.bottom, layout.left)) {
    LOG_DBG("ERS", "Page cache: skip, layout moved since the last render");
    return;
  }

  // Slot 0 covers the page a forward turn lands on, slot 1 the page a backward turn
  // lands on. Both are worth building: a reader turns both ways, and one slot could only
  // ever cover +1, which made every backward turn a guaranteed miss. A reading pace
  // leaves room for both (~700 ms each); an unusually fast test pace cuts the second
  // short, which is what the cancelled() check between them is for.
  auto forward = layout;
  ++forward.page;
  if (forward.page < static_cast<int>(section->pageCount)) buildPageCacheSlot(0, forward, generation);
  if (cancelled() || pageCacheFailed_) return;
  auto backward = layout;
  --backward.page;
  if (backward.page >= 0) buildPageCacheSlot(1, backward, generation);
}

// Build one page into one slot. Returns true when the slot ends up holding a finished
// page; every other exit leaves it Skipped and logs why. Runs only from renderIdle(),
// on the render task while holding the render lock. Cancellation must be bounded.
bool EpubReaderActivity::buildPageCacheSlot(const int slot, const ReaderPageCacheKey& key, const uint32_t generation) {
  const auto cancelled = [&] { return activityManager.idleRenderCancelled(generation); };
  if (pageCache_[slot].attempted(key)) {
    LOG_DBG("ERS", "Page cache: slot %d skip, page %d already attempted (state=%d)", slot, key.page,
            static_cast<int>(pageCache_[slot].state));
    return false;
  }
  pageCache_[slot].key = key;
  pageCache_[slot].state = ReaderPageCache::State::Skipped;

  auto page = section->loadPage(key.page);
  if (!page) {
    LOG_ERR("ERS", "Page cache: slot %d, page %d failed to load", slot, key.page);
    return false;
  }
  if (cancelled()) {
    LOG_DBG("ERS", "Page cache: slot %d, page %d cancelled after the load", slot, key.page);
    return false;
  }
  if (page->hasImages()) {
    if (slot != 0 || !page->hasImagesNeedingDecode()) return false;
    const size_t bytes = renderer.getBufferSize();
    if (!bytes) return false;
    if (!pageCacheStash_[slot]) {
      if (!memory::psramHasHeadroom(bytes, bytes, 16 * 1024)) return false;
      pageCacheStash_[slot] = memory::makePsramByteBufferUninitializedNoThrow(bytes);
      if (!pageCacheStash_[slot]) return false;  // Optional work must not disable the text cache.
    }
    if (cancelled()) return false;
    auto* live = renderer.getFrameBuffer();
    memcpy(pageCacheStash_[slot].get(), live, bytes);
    const auto mode = renderer.getRenderMode();
    const ScopedCleanup restore{[&] {
      renderer.setRenderMode(mode);
      memcpy(live, pageCacheStash_[slot].get(), bytes);
      ImageBlock::releaseRenderCache();
    }};
    uint32_t idleGeneration = generation;
    const CancelCheck cancellation{&idleGeneration, [](void* context) {
                                     return activityManager.idleRenderCancelled(*static_cast<uint32_t*>(context));
                                   }};
    const auto started = millis();
    GfxRenderer::FrameBufferLoan loan(renderer);
    const bool ready = page->warmImages(renderer, key.left, key.top, cancellation);
    LOG_DBG("ERS", "Image prewarm: page=%d ready=%d cancelled=%d time=%lums", key.page, ready,
            cancellation.isCancelled(), millis() - started);
    return false;  // Only .pxc is warmed; foreground still renders the grayscale image.
  }

  const size_t bytes = renderer.getBufferSize();
  if (bytes == 0) return false;
  if (!pageCacheBase_[slot]) {
    constexpr size_t kContiguousReserve = 16 * 1024;
    // Reserve the slots still to allocate; the forward slot is already charged for slot 1.
    if (memory::psramHasHeadroom(static_cast<size_t>(kPageCacheSlots - slot) * 4 * bytes, bytes, kContiguousReserve)) {
      pageCacheBase_[slot] = memory::makePsramByteBufferUninitializedNoThrow(bytes);
      pageCacheLsb_[slot] = memory::makePsramByteBufferUninitializedNoThrow(bytes);
      pageCacheMsb_[slot] = memory::makePsramByteBufferUninitializedNoThrow(bytes);
      if (!pageCacheStash_[slot]) pageCacheStash_[slot] = memory::makePsramByteBufferUninitializedNoThrow(bytes);
    }
    if (!pageCacheBase_[slot] || !pageCacheLsb_[slot] || !pageCacheMsb_[slot] || !pageCacheStash_[slot]) {
      freePageCache();
      pageCacheFailed_ = true;
      LOG_ERR("ERS", "Page cache off: PSRAM allocation of %u B failed", static_cast<unsigned>(4 * bytes));
      return false;
    }
  }
  if (cancelled()) return false;

  uint8_t* const live = renderer.getFrameBuffer();
  auto* fcm = renderer.getFontCacheManager();
  const auto started = millis();
#ifdef ENABLE_CHINESE_VERSION
  if (fcm) fcm->consumeMissingChineseCodepoint();
#endif
  memcpy(pageCacheStash_[slot].get(), live, bytes);
  struct RestoreFrame {
    GfxRenderer& renderer;
    uint8_t* live;
    const uint8_t* stash;
    size_t bytes;
    GfxRenderer::RenderMode mode;
    ~RestoreFrame() {
      renderer.setRenderMode(mode);
      memcpy(live, stash, bytes);
      if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
    }
  } restore{renderer, live, pageCacheStash_[slot].get(), bytes, renderer.getRenderMode()};
  if (fcm) {
    fcm->clearCache();
    fcm->resetStats();
  }

  // Load glyphs on demand: a whole-page prewarm cannot be interrupted. Check
  // between elements instead; an individual SD read/glyph render must finish.
  const auto renderPlane = [&](const GfxRenderer::RenderMode mode, uint8_t* target) {
    if (cancelled()) return false;
    renderer.setRenderMode(mode);
    // clearScreen() reaches the HAL on Read Pico; this is scratch only.
    memset(live, mode == GfxRenderer::BW ? 0xFF : 0x00, bytes);
    drawClippingHighlights(*page, key.spec.fontId, key.top, key.left, key.page);
    GfxRenderer::SyntheticBoldScope syntheticBold(renderer, key.fakeBold);
    for (const auto& element : page->elements) {
      if (cancelled()) return false;
      element->render(renderer, key.spec.fontId, key.left, key.top);
    }
    // Match foreground ordering: all text first, then the guide lines.
    for (const auto& element : page->elements) {
      if (cancelled()) return false;
      if (!key.guideLine || element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      if (line.getBlock()->isEmpty()) continue;
      const int lineHeight = renderer.getLineHeight(key.spec.fontId, key.spec.lineCompression) +
                             line.getBlock()->getRubyShift(renderer.getFontAscenderSize(key.spec.fontId));
      const int guideY = key.top + line.yPos + lineHeight + key.guideOffset;
      if (readingGuideLine::fitsVertically(key.guideStyle, guideY, key.top, renderer.getScreenHeight() - key.bottom)) {
        readingGuideLine::draw(renderer, key.left, guideY, renderer.getScreenWidth() - key.right - 1, key.guideStyle);
      }
    }
    if (cancelled()) return false;
    memcpy(target, live, bytes);
    return !cancelled();
  };

  if (!renderPlane(GfxRenderer::BW, pageCacheBase_[slot].get()) ||
      !renderPlane(GfxRenderer::GRAYSCALE_LSB, pageCacheLsb_[slot].get()) ||
      !renderPlane(GfxRenderer::GRAYSCALE_MSB, pageCacheMsb_[slot].get()) || cancelled())
    return false;
#ifdef ENABLE_CHINESE_VERSION
  pageCacheMissingCodepoint_[slot] = fcm ? fcm->consumeMissingChineseCodepoint() : 0;
#endif
  pageCache_[slot].state = ReaderPageCache::State::Ready;
  LOG_DBG("ERS", "Page cache: slot %d, page %d ready in %lums", slot, key.page, millis() - started);
  return true;
}
#endif

bool EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  bool committed = false;
  struct RefreshCycle {
    int& remaining;
    bool& forced;
    bool& committed;
    int oldRemaining;
    bool oldForced;
    ~RefreshCycle() {
      if (!committed) {
        remaining = oldRemaining;
        forced = forced || oldForced;
      }
    }
  } cycle{pagesUntilFullRefresh, forcedRefreshPending, committed, pagesUntilFullRefresh, forcedRefreshPending};
  const auto finish = [&] { return committed = renderer.displayCommitted(); };
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();
  const auto renderPage = [&] {
    drawClippingHighlights(*page, fontId, orientedMarginTop, orientedMarginLeft);
    GfxRenderer::SyntheticBoldScope syntheticBold(renderer, SETTINGS.fakeBold);
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  };

  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  auto* fcm = renderer.getFontCacheManager();
  const bool pageHasImages = page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  if (pageHasImagesNeedingDecode) {
    // Lend the fixed framebuffer to the large JPEG/PNG decoder while it streams
    // the cold image into .pxc. Formal rendering happens after the loan ends.
    fcm->clearCache();
    fcm->releaseSdFontCaches();
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      page->extractImagesNeedingDecode();
      page->cacheImagesNeedingDecode(renderer, orientedMarginLeft, orientedMarginTop);
    }
    ImageBlock::releaseRenderCache();
    renderer.clearScreen();
  }
  if (pageHasImages) {
    LOG_DBG("ERS", "Image predecode=%lums cold=%u", millis() - t0, static_cast<unsigned>(pageHasImagesNeedingDecode));
  }

#ifdef ENABLE_CHINESE_VERSION
  fcm->consumeMissingChineseCodepoint();
#endif
  struct RawFontCacheGuard {
    FontCacheManager* manager = nullptr;
    ~RawFontCacheGuard() {
      if (manager) manager->clearCache();
    }
  } rawFontCacheGuard;
#if FREEINK_DEVICE_READPICO
  const auto key = pageCacheKey(section ? section->currentPage : -1, orientedMarginTop, orientedMarginRight,
                                orientedMarginBottom, orientedMarginLeft);
  // Either slot may hold this page: slot 0 was built for it as a forward turn, slot 1
  // as a backward one. Whichever matched is recorded, because the three plane copies
  // below must read that slot and not assume one of them.
  bool pageCacheHit = false;
  if (pageCacheEligible()) {
    for (int slot = 0; slot < kPageCacheSlots; ++slot) {
      if (pageCache_[slot].ready(key)) {
        pageCacheLiveSlot_ = slot;
        pageCacheHit = true;
        break;
      }
    }
  }
  ++renderEpoch_;
  renderedPageKey_ = key;
  renderedPageKey_.renderEpoch = renderEpoch_;
#else
  [[maybe_unused]] constexpr bool pageCacheHit = false;
#endif
  std::optional<FontCacheManager::PrewarmScope> prewarmScope;
  // A hit copies finished pixels; it does not need reader glyphs or a scan pass.
  if (!pageCacheHit && fcm->needsPrewarmScan(fontId)) {
    prewarmScope.emplace(*fcm);
    // Scan pass records the page text only (status bar glyphs are flash-resident
    // UI fonts and would otherwise shadow the reader font in the prewarm).
    renderPage();
    prewarmScope->endScanAndPrewarm();
  } else if (!pageCacheHit) {
    fcm->clearCache();
    fcm->resetStats();
    rawFontCacheGuard.manager = fcm;
  }
  const auto tPrewarm = millis();

  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  // Night mode renders crisp B/W; the SDK disables every grayscale display path.
  const bool grayscaleEnabled = !renderer.isInverted() && renderer.grayscaleCapabilities().supported();
  const bool needsTextGrayscale = grayscaleEnabled && SETTINGS.textAntiAliasing;
  // Images write the BW buffer directly; keep them, backgrounds and faux bold on the existing path.
  const bool use16LevelText = needsTextGrayscale && !pageHasImages && renderer.getGrayscaleLevels() == 16 &&
                              sdFontSystem.readerFaceIsFourBit() && !SETTINGS.readingBackgroundEnabled &&
                              SETTINGS.fakeBold == 0;
  const bool needsAnyGrayscale =
      grayscaleEnabled && (needsTextGrayscale || (pageHasImages && !renderer.grayscaleReplacesFrame()));
  const bool absoluteImageGrayscale = grayscaleEnabled && pageHasImages && !gpio.deviceIsX3() &&
                                      display.getController() == HalDisplay::Controller::UC8279 &&
                                      renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  const auto grayscale = renderer.grayscaleCapabilities(absoluteImageGrayscale ? HalDisplay::GrayscaleMode::Absolute
                                                                               : HalDisplay::GrayscaleMode::Overlay);
  const bool tiledGrayscale = needsAnyGrayscale && grayscale.stripUploads;
  // Combined text AA: defer the B/W base activation so
  // the gray planes join it in a single waveform. Displaying the base
  // separately makes the gray pass re-drive the whole text body — a visible
  // flash on every AA page.
  const bool combinedGrayscaleBase = needsAnyGrayscale && !SETTINGS.readingBackgroundEnabled &&
                                     renderer.supportsTextOnlyCombinedBase() &&
                                     (!pageHasImages || !grayscale.stripUploads);
  const bool overlapRefresh = tiledGrayscale && grayscale.asyncBase && !pageHasImages;
  const auto drawGuideLines = [&] {
    if (!SETTINGS.readingGuideLineEnabled) return;
    const int x1 = orientedMarginLeft;
    const int x2 = renderer.getScreenWidth() - orientedMarginRight - 1;
    const int contentBottom = renderer.getScreenHeight() - orientedMarginBottom;
    const int baseLineHeight = renderer.getLineHeight(fontId, SETTINGS.getReaderLineCompression());
    const int ascender = renderer.getFontAscenderSize(fontId);
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      if (line.getBlock()->isEmpty()) continue;
      const int lineHeight = baseLineHeight + line.getBlock()->getRubyShift(ascender);
      const int guideY = orientedMarginTop + line.yPos + lineHeight + SETTINGS.readingGuideLineOffset;
      if (readingGuideLine::fitsVertically(SETTINGS.readingGuideLineStyle, guideY, orientedMarginTop, contentBottom)) {
        readingGuideLine::draw(renderer, x1, guideY, x2, SETTINGS.readingGuideLineStyle);
      }
    }
  };
  const auto renderPageWithGuideLines = [&] {
    renderPage();
    drawGuideLines();
  };
  auto renderGrayscalePass = [&]() {
    // A4 gray planes replace the full page, including its clipping highlights.
    if (absoluteImageGrayscale || needsTextGrayscale) {
      renderPageWithGuideLines();
    } else {
      drawClippingHighlights(*page, fontId, orientedMarginTop, orientedMarginLeft);
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
    if (absoluteImageGrayscale || renderer.grayscaleReplacesFrame()) renderStatusBar();
  };

  if (SETTINGS.readingBackgroundEnabled && !readingBackground::load(renderer)) renderer.clearScreen();

#if FREEINK_DEVICE_READPICO
  if (pageCacheHit) {
    // The cache is the finished page, cleared margins and all, so no clearScreen
    // is needed on this path.
    memcpy(renderer.getFrameBuffer(), pageCacheBase_[pageCacheLiveSlot_].get(), renderer.getBufferSize());

  } else
#endif
  {
    renderPageWithGuideLines();
  }
#ifdef ENABLE_CHINESE_VERSION
#if FREEINK_DEVICE_READPICO
  const uint32_t missingCodepoint =
      pageCacheHit ? pageCacheMissingCodepoint_[pageCacheLiveSlot_] : fcm->consumeMissingChineseCodepoint();
#else
  const uint32_t missingCodepoint = fcm->consumeMissingChineseCodepoint();
#endif
  if (missingCodepoint != 0 && !FontDownloadActivity::wasChineseFontPromptShownThisBoot()) {
    uint32_t expected = 0;
    pendingMissingChineseCodepoint_.compare_exchange_strong(expected, missingCodepoint, std::memory_order_relaxed);
  }
#endif
  renderStatusBar();
  const auto tBwRender = millis();
  if (activityManager.isSwitchPending()) return false;
  // Whole-plane rendering overwrites the shared framebuffer; save its B/W base before deferring any refresh.
  if (needsAnyGrayscale && !tiledGrayscale && !use16LevelText && !renderer.storeBwBuffer()) {
    LOG_ERR("ERS", "OOM: B/W snapshot; displaying B/W page");
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return finish();
  }

  if (absoluteImageGrayscale) {
    const auto baseMode = cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
    if (!renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, baseMode)) {
      LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
      renderer.restoreBwBuffer(false);
      return finish();
    }
    LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
    pagesUntilFullRefresh = 1;
  } else if (use16LevelText) {
    // 16 级文字：下面那一趟 commitGrayscale16() 自己把整帧推给面板，所以这里**不**推底图。
    // 多推一次会变成两次面板驱动（更闪、更慢），而 16 级用的本身就是整屏波形，残影照清。
    // / 16-level text: the single commitGrayscale16() below drives the panel with the whole
    // frame, so the base is deliberately NOT pushed here. Pushing it would drive the panel
    // twice (more flash, more time), and the 16-level waveform is a full-screen one anyway,
    // so ghosting is still cleared.
  } else if (combinedGrayscaleBase) {
    ReaderUtils::displayBaseWithRefreshCycle(renderer, pagesUntilFullRefresh, manualRefreshPending);
  } else if (pageHasImages) {
    // Image pages use one base refresh before the grayscale pass. FAST leaves
    // the panel receptive to the gray waveform; pending cleanup still honors
    // the scheduled/manual HALF refresh.
    if (renderer.supportsContinuousImageReading()) {
      renderer.displayBuffer(ReaderUtils::consumeRefreshMode(pagesUntilFullRefresh),
                             DisplayRefreshContext::ImageReading);
    } else {
      renderer.displayBuffer(cleanImageBasePending ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH,
                             DisplayRefreshContext::ImageReading);
      pagesUntilFullRefresh = 1;
    }
  } else if (needsAnyGrayscale) {
    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else if (overlapRefresh) {
      ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, true);
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
  }
  const auto tDisplay = millis();

  if (use16LevelText) {
    // Native begin clears the frame, so render the complete page again.
    if (renderer.beginGrayscale16()) {
      renderPageWithGuideLines();
      renderStatusBar();
      if (activityManager.isSwitchPending()) {
        renderer.cancelGrayscale16();
        return false;
      }
      if (renderer.commitGrayscale16()) {
        const auto tEnd = millis();
        LOG_DBG("ERS", "Page render: 16-level text total=%lums", tEnd - t0);
        pagesUntilFullRefresh = 1;
        return finish();
      }
      LOG_ERR("ERS", "16-level text commit failed; displaying B/W");
    } else {
      LOG_ERR("ERS", "Could not start the 16-level text pass; displaying B/W");
    }
    renderer.cancelGrayscale16();
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderPageWithGuideLines();
    renderStatusBar();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = 1;
    return finish();
  } else if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;
    const auto abandonGrayscale = [&] {
      renderer.setRenderMode(GfxRenderer::BW);
      if (combinedGrayscaleBase)
        renderer.cancelGrayscale();
      else
        renderer.cleanupGrayscaleWithFrameBuffer();
    };

    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return memory::hasAllocationHeadroom(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), planeBytes, planeBytes,
                                           PLANE_BUF_HEADROOM, PLANE_BUF_MAX_ALLOC_RESERVE);
    };
    const auto allocatePlane = [&] {
      if (planeBufFits()) {
        auto buffer = memory::makeInternalByteBufferNoThrow(planeBytes);
        if (buffer) return buffer;
      }
      if (memory::psramHasHeadroom(planeBytes, planeBytes, PLANE_BUF_MAX_ALLOC_RESERVE)) {
        auto buffer = memory::makePsramByteBufferNoThrow(planeBytes);
        if (buffer) return buffer;
      }
      return memory::ByteBuffer{};
    };
    auto lsbPlaneBuf = overlapRefresh ? allocatePlane() : memory::ByteBuffer{};
    auto msbPlaneBuf = lsbPlaneBuf ? allocatePlane() : memory::ByteBuffer{};

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      renderer.waitRefreshComplete();
      const auto tWait = millis();

      // Abort before expensive grayscale display if a push/pop is pending
      if (activityManager.isSwitchPending()) {
        abandonGrayscale();
        return false;
      }

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
      renderer.waitRefreshComplete();
      if (!scratch) {
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        if (overlapRefresh || combinedGrayscaleBase) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents. On the combined-base path the
          // base activation is still deferred; this cleanup commits it so the
          // page reaches the panel even without its grays.
          if (combinedGrayscaleBase && activityManager.isSwitchPending()) {
            abandonGrayscale();
          } else {
            renderer.cleanupGrayscaleWithFrameBuffer();
          }
        }
      } else {
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          if (activityManager.isSwitchPending()) {
            abandonGrayscale();
            return false;
          }
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch.get(), y, rows);
        }
        const auto tGrayLsb = millis();

        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          if (activityManager.isSwitchPending()) {
            abandonGrayscale();
            return false;
          }
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch.get(), y, rows);
        }
        const auto tGrayMsb = millis();

        if (combinedGrayscaleBase && activityManager.isSwitchPending()) {
          abandonGrayscale();
          return false;
        }
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        const auto tGrayDisplay = millis();

        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();

        const auto tEnd = millis();
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
      }
    }
  } else if (needsAnyGrayscale) {
    const bool displayed = renderWholePlaneGrayscale([&](const bool lsb) {
#if FREEINK_DEVICE_READPICO
      if (pageCacheHit) {
        const auto* plane = lsb ? pageCacheLsb_[pageCacheLiveSlot_].get() : pageCacheMsb_[pageCacheLiveSlot_].get();
        memcpy(renderer.getFrameBuffer(), plane, renderer.getBufferSize());
        return;
      }
#else
      (void)lsb;
#endif
      renderer.clearScreen(renderer.grayPlanesAreAbsolute() ? 0xFF : 0x00);
      renderGrayscalePass();
    });
    committed = displayed;
    return displayed;
  }
  return finish();
}

int EpubReaderActivity::clippingAtPoint(const Page& page, const int x, const int y) const {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int fontId = SETTINGS.getReaderFontId();
  const uint32_t signature = readerRenderSpecSignature(effectiveRenderSpec(buildViewportWidth, buildViewportHeight));
  uint16_t wordIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      const int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                                 block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t index = wordIndex++;
      const Rect rect =
          clippingWordRect(renderer, fontId, line, i, top + SETTINGS.screenMargin, left + SETTINGS.screenMargin, width);
      if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) continue;
      const auto range = block->wordSourceRange(i);
      for (size_t j = 0; j < CLIPPINGS.clippingCount(); ++j) {
        if (clippingContainsWord(*CLIPPINGS.clippingAt(j), currentSpineIndex, section->currentPage, section->pageCount,
                                 signature, index, range.start, range.end))
          return static_cast<int>(j);
      }
    }
  }
  return -1;
}

void EpubReaderActivity::drawClippingHighlights(const Page& page, const int fontId, const int orientedMarginTop,
                                                const int orientedMarginLeft, const int pageNumber) const {
  const int selectedPage = pageNumber >= 0 ? pageNumber : (section ? section->currentPage : -1);
  if (!section || !CLIPPINGS.hasClippings() || selectedPage < 0 || selectedPage >= section->pageCount) {
    return;
  }

  size_t chapterClippings = 0;
  for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
    if (CLIPPINGS.clippingAt(i)->spineIndex == currentSpineIndex) ++chapterClippings;
  }
  if (!chapterClippings) return;

  const auto started = millis();
  size_t highlightedWords = 0;
  const uint16_t currentPage = static_cast<uint16_t>(selectedPage);
  const uint32_t layoutSignature =
      readerRenderSpecSignature(effectiveRenderSpec(buildViewportWidth, buildViewportHeight));
  const auto isHighlighted = [&](const uint16_t wordIndex, const TextBlock::SourceRange range) {
    // ponytail: scan at most 256 C3 / 1024 S3 clippings per word; index by chapter if this becomes slow.
    for (size_t index = 0; index < CLIPPINGS.clippingCount(); ++index) {
      const Clipping& clipping = *CLIPPINGS.clippingAt(index);
      if (clippingContainsWord(clipping, static_cast<uint16_t>(currentSpineIndex), currentPage, section->pageCount,
                               layoutSignature, wordIndex, range.start, range.end))
        return true;
    }
    return false;
  };

  const auto fillHighlight = [&](const int x, const int y, const int width, const int height) {
    if (renderer.grayscaleReplacesFrame() && renderer.getRenderMode() != GfxRenderer::BW) {
      // Gray planes start at zero and use set bits for ink, opposite to B/W.
      for (int py = y + (y & 1); py < y + height; py += 2) {
        for (int px = x + (x & 1); px < x + width; px += 2) renderer.drawPixel(px, py, false);
      }
    } else {
      renderer.fillRectDither(x, y, width, height, Color::LightGray);
    }
  };

  uint16_t pageWordIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    bool hasPreviousHighlight = false;
    int previousHighlightX = 0;
    int previousHighlightWidth = 0;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                           block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t wordIndex = pageWordIndex++;
      if (!isHighlighted(wordIndex, block->wordSourceRange(i))) {
        hasPreviousHighlight = false;
        continue;
      }

      const Rect rect = clippingWordRect(renderer, fontId, line, i, orientedMarginTop, orientedMarginLeft, width);
      const int x = rect.x;
      const int y = rect.y;
      width = rect.width;
      if (width > 0) {
        const int highlightRight = x + width;
        const int previousHighlightRight = previousHighlightX + previousHighlightWidth;
        if (hasPreviousHighlight && previousHighlightRight < x) {
          fillHighlight(previousHighlightRight, y, x - previousHighlightRight, renderer.getLineHeight(fontId));
        } else if (hasPreviousHighlight && highlightRight < previousHighlightX) {
          fillHighlight(highlightRight, y, previousHighlightX - highlightRight, renderer.getLineHeight(fontId));
        }
        fillHighlight(x, y, width, renderer.getLineHeight(fontId));
        previousHighlightX = x;
        previousHighlightWidth = width;
        hasPreviousHighlight = true;
        ++highlightedWords;
      } else {
        hasPreviousHighlight = false;
      }
    }
  }
  LOG_DBG("CLIP", "Highlight pass: chapter_clips=%u highlighted_words=%u time=%lums",
          static_cast<unsigned>(chapterClippings), static_cast<unsigned>(highlightedWords), millis() - started);
}

void EpubReaderActivity::renderStatusBar() const {
  const int currentPage = section ? section->currentPage + 1 : 1;
  const int pageCount = section ? section->estimatedTotalPages() : 1;
  const float bookProgress = bookProgressFor({currentPage - 1, pageCount}, true) * 100;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  const auto note = linkNoteTitle.draw(sb.showsTitle(), bleturner::linkNote());
  if (note == bleturner::LinkNote::Connected) {
    const auto peer = bleturner::linked();
    char connected[64];
    snprintf(connected, sizeof(connected), tr(STR_BT_CONNECTED_TO), peer.name[0] != '\0' ? peer.name : peer.addr);
    title = connected;
  } else if (note == bleturner::LinkNote::LowMemory) {
    title = tr(STR_BT_LOW_MEMORY);
  } else if (note != bleturner::LinkNote::None) {
    title = note == bleturner::LinkNote::Connecting ? tr(STR_BT_CONNECTING) : tr(STR_BT_FAILED);
  } else if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (epub) {
      const int tocIndex = currentTocIndex();
      if (tocIndex != -1) {
        const auto tocItem = epub->getTocItem(tocIndex);
        title = tocItem.title;
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub ? epub->getTitle() : "";
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section ? section->isBuilding() : false);
}

// ---------------------------------------------------------------------------
// Toolbar reader menu
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT,
                                   StrId::STR_FONT_SIZE,
                                   StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT,
                                   StrId::STR_FIRST_LINE_INDENT,
                                   StrId::STR_FOCUS_READING,
                                   StrId::STR_VERTICAL_BOTTOM_ALIGN};
constexpr StrId kSpacingIds[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr StrId kIndentIds[] = {StrId::STR_FIRST_LINE_INDENT_AUTO, StrId::STR_FIRST_LINE_INDENT_INDENT,
                                StrId::STR_FIRST_LINE_INDENT_NO_INDENT};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kSpacingIds) == CrossPointSettings::LINE_COMPRESSION_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
}  // namespace

bool EpubReaderActivity::usesToolbarMenu() const {
  // Both board classes drive the same chrome: touch through the FreeInkUI tap
  // targets, buttons through the focused-tool pill and the panel cursor.
  return SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
}

int EpubReaderActivity::currentTocIndex() const {
  if (!epub) return -1;
  const auto offset = section ? currentPageVisibleOffset : cachedVisibleTextOffset;
  if (Txt::isTxtOrMd(epub->getPath())) return getChapterTocIndex(*epub, currentSpineIndex, offset);
  if (section) return section->getTocIndexForPage(section->currentPage);
  Section cachedSection(epub, currentSpineIndex, renderer);
  return cachedSection.getTocIndexForPage(nextPageNumber);
}

int EpubReaderActivity::currentChapterIndex() const {
  if (!epub) return 0;
  if (Txt::isTxtOrMd(epub->getPath())) return std::max(0, currentTocIndex());
  const int spineIndex = isViewingNonlinearSpine() ? getReadingPosition().spineIndex : currentSpineIndex;
  return std::max(0, epub->getLinearIndexForSpineIndex(spineIndex));
}

bool EpubReaderActivity::gotoChapterIndex(const int target) {
  if (!epub) return false;
  const bool textBook = Txt::isTxtOrMd(epub->getPath());
  const int chapterCount = textBook ? epub->getTocItemsCount() : epub->getLinearSpineItemsCount();
  // A TXT whose headings were never detected has no TOC at all; clamp() with an
  // empty range is undefined, so there is nothing to step to.
  if (chapterCount <= 0) return false;
  const int chapterIndex = currentChapterIndex();
  const int clamped = std::clamp(target, 0, chapterCount - 1);
  if (clamped == chapterIndex && !isViewingNonlinearSpine()) return false;
  RenderLock lock;
  clearDeferredReposition();
  nextPageNumber = 0;
  if (textBook) {
    const auto item = epub->getTocItem(clamped);
    leaveNonlinearSpine(item.spineIndex);
    currentSpineIndex = item.spineIndex;
    pendingAnchor = item.anchor;
  } else {
    const int targetSpineIndex = epub->getSpineIndexForLinearIndex(clamped);
    leaveNonlinearSpine(targetSpineIndex);
    currentSpineIndex = targetSpineIndex;
    pendingAnchor.clear();
  }
  section.reset();
  return true;
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = currentTocIndex();
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return row >= 0 && row < kTextRowCount ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_NOTO_SANS};
  switch (row) {
    case 0:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize) + " pt";
    case 2:
      return I18N.get(kSpacingIds[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    case 4:
      return I18N.get(kIndentIds[SETTINGS.firstLineIndent < std::size(kIndentIds) ? SETTINGS.firstLineIndent : 0]);
    case 5:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case 6:
      return SETTINGS.verticalBottomAlign ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

// Live apply: persist, re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  // Capture the edit as this book's override first. persistBookStyleEdits()
  // puts the global snapshot back into SETTINGS and persists that, then
  // re-applies the overrides in memory, so the typography the user just picked
  // is what gets laid out below while the global settings file stays clean.
  // With per-book memory off it does nothing and the edit persists as global.
  persistBookStyleEdits();
  applyReaderTextSettings();
  discardOverlayPage();  // the stored page is laid out with the old settings
  requestUpdate();
}

// Text panel choices apply immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        if (SETTINGS.fontPointSize == sizes[idx]) return;
        SETTINGS.fontPointSize = sizes[idx];
        SETTINGS.sdFontFlashPreload = 0;
        applyTextSettingLive();
      });
      break;
    }
    case 2:
      overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)),
                        SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT, [this](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 4:
      overlayPopup.show(StrId::STR_FIRST_LINE_INDENT, kIndentIds, static_cast<int>(std::size(kIndentIds)),
                        SETTINGS.firstLineIndent, [this](int idx) {
                          SETTINGS.firstLineIndent = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 5:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      applyTextSettingLive();
      return;
    case 6:
      SETTINGS.verticalBottomAlign = !SETTINGS.verticalBottomAlign;
      applyTextSettingLive();
      return;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

// Push freshly painted overlay chrome. Where the panel supports it the refresh
// is fired deferred: the loop keeps polling input while the waveform runs, so
// the chrome answers taps and buttons the moment it is visible instead of only
// after a blocking displayBuffer() returns. Caller must hold the RenderLock.
void EpubReaderActivity::pushOverlayRefresh() {
  if (renderer.supportsAsyncRefresh()) {
    renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    overlayRefreshPending = true;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

// Wait out a pending deferred overlay refresh and reseed the panel's
// differential baseline from the framebuffer: the shadow-free async path skips
// the post-refresh resync, so without this the next FAST diff would run
// against the frame from before the chrome and leave stale pixels on the
// glass. Caller must hold the RenderLock.
void EpubReaderActivity::settleOverlayRefresh() {
  if (!overlayRefreshPending) return;
  overlayRefreshPending = false;
  renderer.cleanupGrayscaleWithFrameBuffer();  // waits, then reseeds the baseline
}

void EpubReaderActivity::openOverlay(Overlay target) {
  mappedInput.resetHomeButtonInput();
  const Overlay previous = overlay;
  overlay = target;
  if (!toolbarUi) toolbarUi = makeUniqueNoThrow<ReaderToolbarUi>(renderer);
  if (!toolbarUi) {
    LOG_ERR("ERS", "OOM allocating reader toolbar");
    overlay = Overlay::None;
    return;
  }
  if (previous == Overlay::None) toolbarUi->begin();
  // Buttons show a cursor from the start; touch boards only once a button moves it.
  panelCursorShown = !mappedInput.hasTouch();
  switch (target) {
    case Overlay::Toolbar:
      focusedTool = 0;
      break;
    case Overlay::Contents: {
      panelIndex = std::max(0, currentTocIndex());
      // Fresh viewport opening on the current chapter, cursor shown or not.
      toolbarUi->nav().reset(panelIndex);
      toolbarUi->nav().top = panelIndex;
      break;
    }
    case Overlay::Text:
      static_assert(sizeof(SETTINGS.sdFontFamilyName) == 32);
      fontPreview.begin(SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize, SETTINGS.sdFontFlashPreload != 0);
      panelIndex = 0;
      toolbarUi->nav().reset();
      break;
    case Overlay::More:
      panelIndex = 0;
      buildMoreActions();
      toolbarUi->nav().reset();
      break;
    default:
      break;
  }
  panelHoldJumped = false;

  // The page is already on screen and still in the framebuffer, so paint the
  // chrome straight onto it and push one refresh. requestUpdate() would
  // re-render the whole page first: slow, and visibly wrong, since that repaint
  // lands before the overlay does.
  if (section && !pageBufferStale) {
    // Serialize against the render task: renderBook may be mid-page (status
    // bar included) in the shared framebuffer, and painting the chrome from
    // the loop task at the same time interleaves the two frames.
    RenderLock lock;
    settleOverlayRefresh();
    if (previous == Overlay::None) {
      // Snapshot the clean page so stepping back from a panel to the toolbar
      // (and closing, where supported) can restore it without a re-render.
      overlayPageStored = renderer.storeBwBuffer();
    } else if (overlayPageStored) {
      // Overlay -> overlay: wipe the previous chrome (toolbar header, sheet,
      // progress row) back to the clean page so none of it shows around or
      // through the new sheet; re-store for the next transition. No baseline
      // resync: the glass still shows the old chrome, and the differential
      // must keep diffing against it to erase it.
      renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      overlayPageStored = renderer.storeBwBuffer();
    }
    renderOverlay();
    pushOverlayRefresh();
  } else {
    requestUpdate();  // no page in the framebuffer: renderBook() draws the overlay once it is
  }
}

// Grayscale panels re-render to restore AA; other boards restore the B/W snapshot.
void EpubReaderActivity::closeOverlayToPage() {
  mappedInput.resetHomeButtonInput();
  overlay = Overlay::None;
  overlayPopup.dismiss();  // an option picker cannot outlive its panel
  toolbarUi.reset();       // ~1 KB of interaction table + props, only needed while open
  // A panel row toggled in place (image scaling) applies here and only here:
  // the snapshot below holds pixels decoded with the old filter, so drop it to
  // fall through to the re-render path instead of restoring them.
  if (imageScalingDirty) {
    imageScalingDirty = false;
    discardOverlayPage();
  }
  if (renderer.grayscaleReplacesFrame()) pagesUntilFullRefresh = 1;
  if (!xteinkClassPanel() && overlayPageStored) {
    RenderLock lock;  // the render task shares the framebuffer
    settleOverlayRefresh();
    // No baseline resync: the glass shows the chrome, and erasing it needs
    // the differential to keep diffing against the last pushed frame.
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  discardOverlayPage();
  requestUpdate();  // redraw the clean page
}

void EpubReaderActivity::renderOverlay() {
  if (!epub || !section || !toolbarUi) return;

  ReaderToolbarUi::Model model;
  // The toolbar's tool pill is the button-navigation cursor: tap-first (same
  // convention as the panel lists), it only shows once a button has moved it.
  // Panels override below: there the pill marks the open panel on every board.
  model.activeTool = (overlay == Overlay::Toolbar && !panelCursorShown) ? -1 : focusedTool;
  // Strings the model points at live here until render() returns.
  std::string chapterTitle, pageInfo;

  if (overlay == Overlay::Toolbar) {
    chapterTitle = currentChapterTitle();
    const int pageCount = section->estimatedTotalPages();
    const float bookProgress = bookProgressFor({section->currentPage, pageCount}, true);
    pageInfo = std::to_string(section->currentPage + 1) + "/" + std::to_string(pageCount) + "   " +
               std::to_string(clampPercent(static_cast<int>(bookProgress * 100.0f + 0.5f))) + "%";
    model.chapterTitle = chapterTitle.c_str();
    model.pageInfo = pageInfo.c_str();
    model.progressPermille = static_cast<int>(bookProgress * 1000.0f + 0.5f);
    toolbarUi->setModel(model);
    toolbarUi->render();
    return;
  }

  // Panels (Contents / Text / More): a bottom sheet over the page.
  model.panel = true;
  if (!mappedInput.hasTouch()) {
    model.denseRows = true;
  }
  // Tap-first: the cursor is only drawn once a button has moved it, so a
  // tapped row does not stay inverted after its action.
  model.selectedIndex = panelCursorShown ? panelIndex : -1;
  if (overlay == Overlay::Contents) {
    model.panelTitle = tr(STR_TOOL_CONTENTS);
    model.itemCount = epub->getTocItemsCount();
    model.rowText = [this](int i) {
      const auto item = epub->getTocItem(i);
      const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
      return std::string(depth, ' ') + item.title;
    };
  } else if (overlay == Overlay::Text) {
    model.panelTitle = tr(STR_TOOL_TEXT);
    model.itemCount = kTextRowCount;
    model.rowText = [this](int i) { return textRowName(i); };
    model.rowValue = [this](int i) { return textRowValue(i); };
  } else {
    model.panelTitle = tr(STR_TOOL_MORE);
    model.itemCount = static_cast<int>(moreItems.size());
    model.rowText = [this](int i) { return moreRowName(i); };
    model.rowValue = [this](int i) { return moreRowValue(i); };
    model.rowCheckboxContext = this;
    model.rowCheckbox = [](void* ctx, int i, freeink::ui::ListItem& item) {
      const auto* self = static_cast<EpubReaderActivity*>(ctx);
      using MA = EpubReaderMenuActivity::MenuAction;
      const auto action = self->moreItems[i].action;
      if (action == MA::NIGHT_MODE) GUI.setCheckboxRow(item, SETTINGS.screenInverted);
      if (action == MA::BLUETOOTH) GUI.setCheckboxRow(item, SETTINGS.ble.enabled);
      if (action == MA::FRONTLIGHT) GUI.setCheckboxRow(item, Frontlight.isOn());
    };
  }
  toolbarUi->setModel(model);
  toolbarUi->render();
}

void EpubReaderActivity::handleOverlayInput() {
  if (!toolbarUi) return;

  // A modal option picker over the panel owns all input while open.
  if (overlayPopup.isActive()) {
    overlayPopup.handleInput(mappedInput, [this] {
      if (overlayPopup.isActive()) {
        paintOverlayPopup();  // highlight moved
        return;
      }
      // Dismissed or selected: erase the dialog -- clean page back, then the
      // panel over it (the dialog can overhang the sheet onto the page).
      RenderLock lock;
      settleOverlayRefresh();
      if (overlayPageStored) {
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
        renderOverlay();
        pushOverlayRefresh();
      } else {
        requestUpdate();
      }
    });
    return;
  }
  const auto fastRedraw = [this] {
    RenderLock lock;  // the render task shares the framebuffer
    settleOverlayRefresh();
    renderOverlay();
    pushOverlayRefresh();
  };

  const bool textBook = Txt::isTxtOrMd(epub->getPath());
  const int chapterCount = textBook ? epub->getTocItemsCount() : epub->getLinearSpineItemsCount();
  const int chapterIndex = currentChapterIndex();
  const auto gotoChapter = [this](const int target) {
    // Shared with the tap zones (gotoChapterIndex): TXT/MD chapters are anchors
    // inside one spine, so they cannot be reached by stepping the spine.
    gotoChapterIndex(target);
    requestUpdate();
  };
  const auto toolOverlay = [](int tool) {
    return tool == 0 ? Overlay::Contents : (tool == 1 ? Overlay::Text : Overlay::More);
  };

  // Touch first: FreeInkUI routes the frame against the tap targets the last
  // render registered and hands back the action it mapped to.
  const auto routed = toolbarUi->route(mappedInput);

  // --- Toolbar ---
  if (overlay == Overlay::Toolbar) {
    switch (routed.event) {
      case ReaderToolbarUi::Event::Dismiss:
        closeOverlayToPage();
        return;
      case ReaderToolbarUi::Event::Tool:
        focusedTool = routed.value;
        openOverlay(toolOverlay(focusedTool));
        return;
      case ReaderToolbarUi::Event::PrevChapter:
        gotoChapter(chapterIndex - 1);
        return;
      case ReaderToolbarUi::Event::NextChapter:
        gotoChapter(chapterIndex + 1);
        return;
      case ReaderToolbarUi::Event::Scrub:
        gotoChapter(static_cast<int>(
            (static_cast<float>(routed.permille) / 1000.0f) * static_cast<float>(chapterCount - 1) + 0.5f));
        return;
      default:
        break;
    }
    if (routed.routed) return;  // a touch frame the chrome consumed (or dead space)

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closeOverlayToPage();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      focusedTool = (focusedTool + 2) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      focusedTool = (focusedTool + 1) % 3;
      panelCursorShown = true;
      fastRedraw();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openOverlay(toolOverlay(focusedTool));
      return;
    }
    const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up);
    const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down);
    if (prev || next) {
      gotoChapter(chapterIndex + (next ? 1 : -1));
    }
    return;
  }

  // --- Panels (Contents / Text / More) ---
  const int count = overlay == Overlay::Contents ? epub->getTocItemsCount()
                    : overlay == Overlay::Text   ? kTextRowCount
                                                 : static_cast<int>(moreItems.size());
  const int pageRows = std::max(1, toolbarUi->visibleRows());

  // Activate the highlighted row: change a value / jump to a chapter / run an
  // action. Shared by the Confirm button and a row tap.
  const auto activateRow = [this, count, &fastRedraw] {
    if (panelIndex < 0 || panelIndex >= count) return;
    if (overlay == Overlay::Text) {
      if (panelIndex == 0) {
        // Full font picker (built-in + SD fonts, live preview) -- the same
        // screen Settings uses; a popup cannot scroll a long font list.
        overlay = Overlay::None;
        overlayPopup.dismiss();
        discardOverlayPage();
        {
          RenderLock lock;
          settleOverlayRefresh();
        }
        auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(
            renderer, mappedInput, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
            TextSettingsActivity::InitialFontState::Unchanged, TextSettingsActivity::StartMode::PreviewOnly, &bookPath,
            globalSettingsSnapshotted_ ? &globalSettingsSnapshot_ : nullptr);
        if (!textSettings) {
          LOG_ERR("ERS", "OOM allocating text settings");
          overlay = Overlay::Text;
          return;
        }
        startActivityForResult(std::move(textSettings), [this](const ActivityResult&) {
          // Same contract as the reader menu's text-settings result: with
          // per-book memory on, the edits become this book's overrides and the
          // global file keeps the entered snapshot (this also covers the page
          // where the user turned the switch off).
          persistBookStyleEdits();
          applyReaderTextSettings();
          overlay = Overlay::Text;  // back to the Text panel
          panelIndex = 0;
          if (toolbarUi) toolbarUi->begin();  // the picker drew its own FUI screen
          requestUpdate();                    // re-render page + Text panel
        });

      } else {
        showTextRowPopup(panelIndex);
      }
    } else if (overlay == Overlay::Contents) {
      const auto item = epub->getTocItem(panelIndex);
      if (item.spineIndex != -1) {
        RenderLock lock;
        clearDeferredReposition();
        leaveNonlinearSpine(item.spineIndex);
        currentSpineIndex = item.spineIndex;
        pendingAnchor = item.anchor;
        nextPageNumber = 0;
        section.reset();
      }
      overlay = Overlay::None;
      discardOverlayPage();
      requestUpdate();
    } else if (overlay == Overlay::More) {
      activateMoreRow(panelIndex);
    }
  };

  // Steps up to the toolbar -- the Back button and a tap on the page above
  // the sheet.
  const auto dismissPanel = [this, &fastRedraw] {
    overlay = Overlay::Toolbar;
    // Restore the snapshotted page under the toolbar instead of re-rendering
    // it (2+ refreshes -> one FAST). Re-store right away so another panel
    // round-trip can restore again.
    if (overlayPageStored) {
      {
        RenderLock lock;  // the render task shares the framebuffer
        settleOverlayRefresh();
        // No baseline resync: the glass shows the panel, and erasing it needs
        // the differential to keep diffing against the last pushed frame.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      fastRedraw();  // takes its own RenderLock
      return;
    }
    requestUpdate();
  };

  // Pages the list by one screen of rows through the nav (measured page size,
  // no-op at the ends). A shown cursor rides along so the buttons continue
  // from what is visible; on touch boards only the viewport moves.
  const auto pageList = [this, count, pageRows, &fastRedraw](int direction) {
    if (count <= 0) return;
    const bool moved = toolbarUi->nav().scrollBy(direction * pageRows, count);
    if (panelCursorShown) {
      panelIndex = std::clamp(panelIndex + direction * pageRows, 0, count - 1);
      fastRedraw();
      return;
    }
    if (moved) fastRedraw();
  };

  switch (routed.event) {
    case ReaderToolbarUi::Event::Dismiss:
      dismissPanel();
      return;
    case ReaderToolbarUi::Event::Tool: {
      // Sheet-bottom tool switcher: hop straight to another panel.
      const Overlay target = toolOverlay(routed.value);
      if (target != overlay) {
        focusedTool = routed.value;
        openOverlay(target);
      }
      return;
    }
    case ReaderToolbarUi::Event::Row:
      // A tap on the right-edge strip pages the sheet instead (upper half =
      // previous page, lower half = next): swipes are unreliable on etched
      // glass, and a long contents list needs a fast way through.
      if (routed.x >= renderer.getScreenWidth() - 44) {
        pageList(routed.y >= renderer.getScreenHeight() - (renderer.getScreenHeight() * 62) / 200 ? 1 : -1);
        return;
      }
      panelIndex = routed.value;
      panelCursorShown = false;
      activateRow();
      return;
    default:
      break;
  }
  // Swipe up/down pages the list. Checked before the routed-frame return:
  // FUI routes every touch frame over the sheet, so a swipe's frames count as
  // routed (without dispatching -- too much travel for a tap) and the gesture
  // would otherwise never be seen.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    pageList(swipe == MappedInputManager::SwipeDir::Up ? 1 : -1);
    return;
  }
  if (routed.routed) return;  // consumed by the chrome (title band, dead space)

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    dismissPanel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow();
    return;
  }

  // Up/Down (side) and Left/Right (front) move the cursor: a tap steps one
  // row, holding past PANEL_HOLD_MS jumps PANEL_HOLD_STEP rows in one go, which
  // is how you cross a hundreds-of-chapters contents list without a press per
  // row. The jump fires once on the hold and swallows the release that ends it,
  // so it never doubles up with the tap step.
  if (count > 0) {
    const bool up = mappedInput.isPressed(MappedInputManager::Button::Up) ||
                    mappedInput.isPressed(MappedInputManager::Button::Left);
    const bool down = mappedInput.isPressed(MappedInputManager::Button::Down) ||
                      mappedInput.isPressed(MappedInputManager::Button::Right);
    if (!panelHoldJumped && (up || down) && mappedInput.getHeldTime() >= PANEL_HOLD_MS) {
      const int step = down ? PANEL_HOLD_STEP : -PANEL_HOLD_STEP;
      panelIndex = std::clamp(panelIndex + step, 0, count - 1);
      panelHoldJumped = true;
      panelCursorShown = true;
      fastRedraw();
      return;
    }

    const bool releasedUp = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool releasedDown = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (releasedUp || releasedDown) {
      if (!panelHoldJumped) {
        panelIndex = releasedUp ? ButtonNavigator::previousIndex(panelIndex, count)
                                : ButtonNavigator::nextIndex(panelIndex, count);
        panelCursorShown = true;
        fastRedraw();
      }
      panelHoldJumped = false;
    }
  }
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock;
  settleOverlayRefresh();
  overlayPopup.render(renderer);
  pushOverlayRefresh();
}

// Auto page-turn rate picker, opened from the More panel and from the tap
// zones. The option list matches the reader menu (Off / 3 / 6 / 12 / Custom)
// so both entry points offer exactly the same rates.
void EpubReaderActivity::showAutoPageTurnPopup() {
  static constexpr int kCustomOption = 4;  // index of the Custom entry
  std::vector<std::string> labels;
  labels.reserve(std::size(PAGE_TURN_RATES) + 1);
  labels.emplace_back(tr(STR_STATE_OFF));
  for (size_t i = 1; i < std::size(PAGE_TURN_RATES); ++i) labels.push_back(std::to_string(PAGE_TURN_RATES[i]));
  labels.emplace_back(tr(STR_CUSTOM));
  const int current = (autoTurnOption == kCustomOption) ? kCustomOption : (autoTurnOption < 0 ? 0 : autoTurnOption);
  overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels, current, [this](int idx) {
    if (idx == kCustomOption) {
      // Custom rate reuses the same interval picker as the reader menu.
      startActivityForResultWith<IntervalSelectionActivity>(
          [this](const ActivityResult& result) {
            READING_STATS.resumeSession();
            if (!result.isCancelled) {
              customAutoPageTurnRate_ = std::get<IntervalResult>(result.data).value;
              autoTurnOption = kCustomOption;
              toggleAutoPageTurn(customAutoPageTurnRate_);
            }
            requestUpdate();
          },
          "AutoPageTurnRate", StrId::STR_AUTO_TURN_PAGES_PER_MIN, customAutoPageTurnRate_, 1, MAX_PAGE_TURN_RATE, 1, 5);
      return;
    }
    autoTurnOption = idx;
    toggleAutoPageTurn(static_cast<uint8_t>(PAGE_TURN_RATES[idx]));
  });
  paintOverlayPopup();
}

void EpubReaderActivity::applyReaderTextSettings() {
  // Shared reflow path for every reader entry point (toolbar picker, reader
  // menu, live apply). It must use the book-style persistence contract: with
  // per-book memory on, SETTINGS carries this book's typography, so a raw
  // saveToFile() here would write it into the global settings file right after
  // the caller's persistBookStyleEdits() restored the global values.
  {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    // Release the old parser and page table before settings serialization needs heap.
    section.reset();  // force re-pagination with the new settings
  }
  saveSettingsKeepingBookStyle();
  RenderLock lock;
  // (Re)load or unload the selected SD-card font for the current family/size.
  // The reader otherwise only loads SD fonts on book open, so without this an
  // in-reader font change wouldn't take effect until re-opening the book.
  // The existing lock also serializes SD font variants against rendering.
  sdFontSystem.ensureLoaded(renderer);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }
  section.reset();  // force re-pagination with the new settings
}

void EpubReaderActivity::finishFontPreview() {
  const auto* family = sdFontSystem.registry().findFamily(SETTINGS.sdFontFamilyName);
  const auto* file = family ? family->findNearestSize(SETTINGS.fontPointSize) : nullptr;
  const bool supportsPreload = !family || !family->vector;
  char path[SdCardFontFamilyInfo::PATH_BUFFER_SIZE];
  const auto check = file && supportsPreload && family->filePath(*file, path, sizeof(path))
                         ? SdCardFontCache::preflight(path)
                         : SdCardFontCache::Result::InvalidFont;
  const auto decision = fontPreview.finish(SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize,
                                           check == SdCardFontCache::Result::AlreadyCached, supportsPreload);
  switch (decision) {
    case ReaderFontPreview::Decision::Keep:
      return;
    case ReaderFontPreview::Decision::Enable: {
      RenderLock lock;
      const bool reload = SETTINGS.sdFontFlashPreload == 0;
      SETTINGS.sdFontFlashPreload = 1;
      // ensureLoaded's same-family/size fast path does not change the source.
      if (reload) sdFontSystem.releaseLoadedFont(renderer);
      sdFontSystem.ensureLoaded(renderer);
      break;
    }
    case ReaderFontPreview::Decision::Disable:
    case ReaderFontPreview::Decision::Ask:
      SETTINGS.sdFontFlashPreload = 0;
      break;
  }
  saveSettingsKeepingBookStyle();
  if (decision != ReaderFontPreview::Decision::Ask) return;

  if (check == SdCardFontCache::Result::TooLarge) {
    {
      RenderLock lock;
      fontPromptState = FontPromptState::TooLarge;
    }
    requestUpdateAndWait();
    fontNoticeStartedAt = millis();
    return;
  }
  {
    RenderLock lock;
    fontPromptState = FontPromptState::Asking;
    fontPromptWaitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
    if (check == SdCardFontCache::Result::Ok) {
      constexpr StrId options[] = {StrId::STR_FONT_PRELOAD_START, StrId::STR_FONT_PRELOAD_SKIP};
      overlayPopup.show(StrId::STR_FONT_PRELOAD_CONFIRM, options, static_cast<int>(std::size(options)), 0,
                        [this](int index) {
                          RenderLock lock;
                          if (index == 0) fontPromptState = FontPromptState::Accepted;
                        });
    } else {
      constexpr StrId options[] = {StrId::STR_OK_BUTTON};
      overlayPopup.show(fontpreload::failureMessage(check), options, static_cast<int>(std::size(options)), 0, {});
    }
  }
  requestUpdate();
}

void EpubReaderActivity::handleFontPreloadPrompt() {
  if (fontPromptState == FontPromptState::TooLarge) {
    if (millis() - fontNoticeStartedAt >= fontpreload::NOTICE_DURATION_MS) {
      for (uint8_t button = 0; button < MappedInputManager::kButtonCount; ++button) {
        if (mappedInput.isPressed(static_cast<MappedInputManager::Button>(button))) return;
      }
      int x = 0, y = 0;
      if (mappedInput.isScreenTouchHeld(x, y)) return;
      {
        RenderLock lock;
        fontPromptState = FontPromptState::Idle;
      }
      requestUpdate();
    }
    return;
  }
  if (fontPromptWaitForBackRelease) {
    fontPromptWaitForBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
    return;  // Consume the inherited release as well as the hold.
  }
  overlayPopup.handleInput(mappedInput, [this] { requestUpdate(); });
  if (overlayPopup.isActive()) return;

  const bool accepted = fontPromptState == FontPromptState::Accepted;
  {
    RenderLock lock;
    fontPromptState = FontPromptState::Idle;
    overlayPopup.dismiss();
  }
  if (!accepted) {
    requestUpdate();
    return;
  }

  // ActivityManager owns this existing progress activity across loop frames;
  // stack storage cannot outlive this call. No second font/cache buffer is added.
  auto preload = makeUniqueNoThrow<TextSettingsActivity>(
      renderer, mappedInput, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
      TextSettingsActivity::InitialFontState::Changed, TextSettingsActivity::StartMode::PreloadThenExit);
  if (!preload) {
    LOG_ERR("ERS", "OOM allocating font preload activity (%zu bytes)", sizeof(TextSettingsActivity));
    SETTINGS.sdFontFlashPreload = 0;
    saveSettingsKeepingBookStyle();
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(preload), [this](const ActivityResult&) {
    {
      RenderLock lock;
      // Also covers a cache that became valid between the prompt and preload.
      sdFontSystem.releaseLoadedFont(renderer);
      sdFontSystem.ensureLoaded(renderer);
    }
    lastPageTurnTime = millis();
    requestUpdate();
  });
}

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() {
  using MA = EpubReaderMenuActivity::MenuAction;
  EpubReaderMenuActivity::buildMenuItems(moreItems, !currentPageFootnotes.empty(), !cachedBookmarks.empty(),
                                         CLIPPINGS.hasClippings());
  moreItems.erase(std::remove_if(moreItems.begin(), moreItems.end(),
                                 [](const auto& item) {
                                   return item.action == MA::SELECT_CHAPTER || item.action == MA::TEXT_SETTINGS;
                                 }),
                  moreItems.end());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      if (autoTurnOption == 4) return std::to_string(customAutoPageTurnRate_);
      return (autoTurnOption == 0 || autoTurnOption >= static_cast<int>(std::size(PAGE_TURN_RATES)))
                 ? std::string(tr(STR_STATE_OFF))
                 : std::to_string(PAGE_TURN_RATES[autoTurnOption]);
    case MA::NIGHT_MODE:
      return SETTINGS.screenInverted ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case MA::FRONTLIGHT:
      return Frontlight.isOn() ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case MA::IMAGE_SCALING: {
      // Same labels the reader menu's option popup offers, so the row's value
      // and the popup's highlighted entry always agree.
      static constexpr StrId kScaling[] = {StrId::STR_IMAGE_SCALING_NEAREST, StrId::STR_IMAGE_SCALING_BILINEAR};
      static_assert(std::size(kScaling) == CrossPointSettings::IMAGE_SCALING_COUNT, "image scaling labels");
      const size_t mode = SETTINGS.imageScaling < CrossPointSettings::IMAGE_SCALING_COUNT ? SETTINGS.imageScaling : 0;
      return I18N.get(kScaling[mode]);
    }
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  {
    RenderLock lock;  // several actions launch screens that paint the framebuffer
    settleOverlayRefresh();
  }
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::AUTO_PAGE_TURN:
      showAutoPageTurnPopup();
      return;
    case MA::IMAGE_SCALING: {
      // A plain on/off-style row like the frontlight one: one tap cycles, no
      // picker. The value only reaches the page when the overlay closes, so the
      // row repaints the panel over the page snapshot instead of re-rendering
      // the page (which would decode the image twice in a row).
      const uint8_t current =
          SETTINGS.imageScaling < CrossPointSettings::IMAGE_SCALING_COUNT ? SETTINGS.imageScaling : 0;
      const uint8_t next = static_cast<uint8_t>((current + 1) % CrossPointSettings::IMAGE_SCALING_COUNT);
      SETTINGS.imageScaling = next;
      saveSettingsKeepingBookStyle();
      imageScalingDirty = true;
      LOG_INF("ERS", "Image scaling -> %s",
              next == CrossPointSettings::IMAGE_SCALING_BILINEAR ? "bilinear" : "nearest");
      RenderLock lock;  // the render task shares the framebuffer
      if (overlayPageStored) {
        // Clean page back first (the sheet's old value text is not
        // background-filled), then re-snapshot it and lay the panel on top.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        overlayPageStored = renderer.storeBwBuffer();
      }
      renderOverlay();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    }
    case MA::BLUETOOTH:
      EpubReaderMenuActivity::toggleBluetooth();
      // Save through the book-style path: with per-book memory on, SETTINGS
      // carries this book's typography and a plain save would write the book's
      // overrides into the global settings file.
      saveSettingsKeepingBookStyle();
      {
        RenderLock lock;
        renderOverlay();
        pushOverlayRefresh();
      }
      return;
    case MA::NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      // Night mode is a global setting, but with per-book memory on SETTINGS
      // currently carries this book's typography overrides: save the entered
      // global snapshot instead, or the book's font would land in the global
      // settings file and survive a restart.
      saveSettingsKeepingBookStyle();
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      saveSettingsKeepingBookStyle();
      {
        RenderLock lock;  // the render task shares the framebuffer
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  overlay = Overlay::None;
  if (action == MA::GO_TO_PERCENT && overlayPageStored) {
    // The percent dialog is a popup over the current frame: wipe the toolbar
    // chrome back to the clean page first so the dialog draws over the page,
    // not the sheet. No refresh push — the dialog's first frame carries it.
    RenderLock lock;
    settleOverlayRefresh();
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
  } else {
    discardOverlayPage();
  }
  if (action == MA::TOGGLE_BOOKMARK) {
    // No child activity here to trigger the re-render the list menu relies on:
    // show the same confirmation popup the long-press path does.
    addBookmark();
    showBookmarkMessage = true;
    bookmarkMessageTime = millis();
    requestUpdate();
    return;
  }
  onReaderMenuConfirm(action);
  // Child screens re-render the reader on Pop; GO_HOME/DELETE_CACHE leave the
  // reader entirely. In-place actions would otherwise leave the closed panel
  // on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE) requestUpdate();
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';
  int targetSpineIndex = sameFile ? currentSpineIndex : epub->resolveHrefToSpineIndex(hrefStr);

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    return;
  }
  if (savePosition && section &&
      (currentSpineIndex != renderedSpineIndex_ || section->currentPage != committedPageNumber_)) {
    return;
  }

  if (isViewingNonlinearSpine() && epub->isLinearSpineItem(targetSpineIndex)) {
    leaveNonlinearSpine(targetSpineIndex);
  } else if (savePosition && !pushSavedPosition()) {
    return;
  }

  {
    RenderLock lock;
    clearDeferredReposition();
    pendingOffsetJump.reset();
    pendingPageJump.reset();
    pendingPercentJump = false;
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

bool EpubReaderActivity::pushSavedPosition() {
  if (section && (currentSpineIndex != renderedSpineIndex_ || section->currentPage != committedPageNumber_)) {
    return false;
  }
  // Full: drop the oldest entry so Back always returns from the newest jump.
  if (footnoteDepth == MAX_FOOTNOTE_DEPTH) {
    // A nested note visit must retain its only route back to the reading text.
    int drop = 0;
    if (isViewingNonlinearSpine()) {
      for (int i = footnoteDepth - 1; i >= 0; --i) {
        if (!epub->isLinearSpineItem(savedPositions[i].spineIndex)) continue;
        if (i == 0) drop = 1;
        break;
      }
    }
    std::move(savedPositions + drop + 1, savedPositions + MAX_FOOTNOTE_DEPTH, savedPositions + drop);
    footnoteDepth--;
  }
  // A child screen (menu, footnote list) may have released the section;
  // nextPageNumber then holds the page it was on.
  const int page = section ? section->currentPage : nextPageNumber;
  const int pageCount = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  const auto offset =
      section ? (currentPageVisibleOffset ? currentPageVisibleOffset : section->getVisibleTextOffsetForPage(page))
              : cachedVisibleTextOffset;
  savedPositions[footnoteDepth] = {currentSpineIndex, page, offset, pageCount};
  footnoteDepth++;
  LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, page);
  return true;
}

bool EpubReaderActivity::isViewingNonlinearSpine() const {
  return epub && currentSpineIndex >= 0 && currentSpineIndex < epub->getSpineItemsCount() &&
         !epub->isLinearSpineItem(currentSpineIndex);
}

void EpubReaderActivity::leaveNonlinearSpine(const int targetSpineIndex) {
  if (!isViewingNonlinearSpine() || targetSpineIndex < 0 || targetSpineIndex >= epub->getSpineItemsCount() ||
      !epub->isLinearSpineItem(targetSpineIndex)) {
    return;
  }
  footnoteDepth = 0;
  pendingAnchor.clear();
  pendingOffsetJump.reset();
  pendingPageJump.reset();
  pendingPercentJump = false;
}

CrossPointPosition EpubReaderActivity::getReadingPosition() const {
  if (!isViewingNonlinearSpine()) return getCurrentPosition();
  CrossPointPosition position{epub->getSpineIndexForLinearIndex(0), 0, 0};
  for (int i = footnoteDepth - 1; i >= 0; --i) {
    const auto& saved = savedPositions[i];
    if (saved.spineIndex < 0 || saved.spineIndex >= epub->getSpineItemsCount() ||
        !epub->isLinearSpineItem(saved.spineIndex)) {
      continue;
    }
    position = {saved.spineIndex, saved.pageNumber, saved.pageCount};
    if (saved.visibleTextOffset) {
      position.visibleTextOffset = *saved.visibleTextOffset;
      position.hasVisibleTextOffset = true;
    }
    break;
  }
  position.hasResolvedSpineIndex = position.spineIndex >= 0;
  position.hasMappedPage = position.spineIndex >= 0;
  return position;
}

void EpubReaderActivity::saveLinkStack() const {
  // [depth] then depth x (spine u16 LE, page u16 LE)
  uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
  size_t size = 0;
  data[size++] = static_cast<uint8_t>(footnoteDepth);
  for (int i = 0; i < footnoteDepth; i++) {
    const SavedPosition& pos = savedPositions[i];
    data[size++] = pos.spineIndex & 0xFF;
    data[size++] = (pos.spineIndex >> 8) & 0xFF;
    data[size++] = pos.pageNumber & 0xFF;
    data[size++] = (pos.pageNumber >> 8) & 0xFF;
  }
  HalFile f;
  if (!Storage.openFileForWrite("ERS", epub->getCachePath() + "/links.bin", f)) return;
  if (f.write(data, size) != size) LOG_ERR("ERS", "Failed to write link stack");
}

void EpubReaderActivity::loadLinkStack() {
  const std::string path = epub->getCachePath() + "/links.bin";
  if (!Storage.exists(path.c_str())) return;
  {
    HalFile f;
    uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
    if (Storage.openFileForRead("ERS", path, f)) {
      const int size = f.read(data, sizeof(data));
      const int depth = size > 0 ? data[0] : 0;
      if (depth >= 1 && depth <= MAX_FOOTNOTE_DEPTH && size == 1 + depth * 4) {
        const int spineCount = epub->getSpineItemsCount();
        bool valid = true;
        for (int i = 0; i < depth; i++) {
          const uint8_t* p = data + 1 + i * 4;
          savedPositions[i] = {p[0] | (p[1] << 8), p[2] | (p[3] << 8)};
          valid = valid && savedPositions[i].spineIndex < spineCount;
        }
        if (valid) {
          footnoteDepth = depth;
          LOG_DBG("ERS", "Loaded link stack, depth %d", depth);
        }
      }
    }
  }
  // Consumed once: a later exit rewrites it, and an unclean shutdown must not
  // resurrect a stale stack.
  Storage.remove(path.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  SavedPosition pos;
  if (footnoteDepth > 0) {
    pos = savedPositions[--footnoteDepth];
  } else {
    if (!isViewingNonlinearSpine()) return;
    const auto readingPosition = getReadingPosition();
    if (readingPosition.spineIndex < 0) return;
    pos = {readingPosition.spineIndex, readingPosition.pageNumber, std::nullopt, readingPosition.totalPages};
  }
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock;
    clearDeferredReposition();
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    pendingAnchor.clear();
    pendingPercentJump = false;
    pendingPageJump.reset();
    pendingOffsetJump = pos.visibleTextOffset;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) return;
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock;
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      int pct = bookPercentFor({section->currentPage, info.totalPages});
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

int EpubReaderActivity::getProgressBasisPoints() const {
  if (isAtEndOfBook()) return 10000;
  if (!epub || !section || epub->getBookSize() == 0) return getProgressPercent() * 100;
  const int totalPages = section->estimatedTotalPages();
  if (totalPages <= 0) return getProgressPercent() * 100;
  const int basisPoints = static_cast<int>(bookProgressFor({section->currentPage, totalPages}) * 10000.0f + 0.5f);
  return std::clamp(basisPoints, 0, 10000);
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  localPos.hasResolvedSpineIndex = true;
  localPos.hasMappedPage = true;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}
