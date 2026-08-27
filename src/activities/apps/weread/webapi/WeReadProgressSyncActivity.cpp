#include "WeReadProgressSyncActivity.h"

#ifdef ENABLE_CHINESE_VERSION

#include <Epub.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "ProgressMapper.h"
#include "ReadingStatsStore.h"
#include "SilentRestart.h"
#include "WeReadXhtmlCodec.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/ReaderUtils.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {
static_assert(sizeof(WeReadClient::Operation) <= 8 * 1024, "WeRead progress workspace exceeds its fixed heap budget");
}

WeReadProgressSyncActivity::WeReadProgressSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string epubPath, const char* bookId,
                                                       const WeReadProgressContext& context)
    : Activity("WeReadProgressSync", renderer, mappedInput), epubPath_(std::move(epubPath)) {
  if (bookId) strncpy(bookId_, bookId, sizeof(bookId_) - 1);
  input_.localFraction = context.localFraction;
  input_.localTocIndex = context.localTocIndex;
  input_.localOffset = context.localOffset;
  input_.localOffsetBasis = context.localOffsetBasis;
  localSpineIndex_ = context.localSpineIndex;
  localPageNumber_ = context.localPageNumber;
  localPageCount_ = context.localPageCount;
}

WeReadProgressContext WeReadProgressSyncActivity::makeContext(const Epub& epub, const char* bookId,
                                                              const float localFraction,
                                                              const CrossPointPosition& localPosition) {
  WeReadProgressContext context;
  context.localFraction = localFraction;
  context.localSpineIndex = static_cast<uint16_t>(localPosition.spineIndex);
  context.localPageNumber = static_cast<uint16_t>(localPosition.pageNumber);
  context.localPageCount = static_cast<uint16_t>(localPosition.totalPages);
  if (!localPosition.hasVisibleTextOffset) return context;

  const std::string bookDir = WeReadStore::bookDirectory(bookId);
  WeReadStore::BookOptions options;
  if (!WeReadStore::loadBookOptions(bookDir, options) ||
      !WeReadStore::parseGeneratedChapterHref(epub.getSpineItem(localPosition.spineIndex).href,
                                              context.localTocIndex)) {
    return context;
  }

  context.localOffset = localPosition.visibleTextOffset;
  context.localOffsetBasis = WeReadClient::LocalOffsetBasis::VisibleText;
  uint32_t nativeOffset = 0;
  if (WeReadXhtmlCodec::visibleToNativeOffset(WeReadStore::chapterPath(bookDir, context.localTocIndex),
                                              context.localOffset, nativeOffset)) {
    context.localOffset = nativeOffset;
    context.localOffsetBasis = WeReadClient::LocalOffsetBasis::RawXhtmlUtf16;
  }
  return context;
}

void WeReadProgressSyncActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  WeReadStore::Session session;
  const bool loggedIn = WeReadStore::loadSession(session) && session.valid();
  session.clear();
  if (!loggedIn) {
    state_ = State::LoginRequired;
    requestUpdate();
    return;
  }

  NetworkStartup::prepare(renderer);
  wifiActivated_ = true;
  if (WiFi.status() == WL_CONNECTED) {
    state_ = State::Starting;
    requestUpdate();
    return;
  }
  launchWifiSelection();
}

void WeReadProgressSyncActivity::onExit() {
  operation_.reset();
  Activity::onExit();
  if (!wifiActivated_) return;
  WiFi.disconnect(false);
  delay(30);
  silentRestartToReader();
}

bool WeReadProgressSyncActivity::preventAutoSleep() { return state_ == State::Starting || state_ == State::Syncing; }

void WeReadProgressSyncActivity::launchWifiSelection() {
  state_ = State::WifiSelection;
  // ActivityManager owns this fixed-size activity until its result returns.
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true);
  if (!wifi) {
    LOG_ERR("WRSync", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    error_ = WeReadClient::Error::OutOfMemory;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void WeReadProgressSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected || WiFi.status() != WL_CONNECTED) {
    returnToReader();
    return;
  }
  state_ = State::Starting;
  requestUpdate();
}

void WeReadProgressSyncActivity::startSync() {
  requestUpdateAndWait();
  if (!TimeUtils::isClockValid() && !halClock.syncNow()) {
    LOG_ERR("WRSync", "Clock sync failed");
    error_ = WeReadClient::Error::Clock;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  if (!operation_.beginProgressSync(bookId_, input_, syncMode_)) {
    error_ = operation_.error();
    state_ = error_ == WeReadClient::Error::SessionExpired ? State::LoginRequired : State::Failed;
    requestUpdate();
    return;
  }
  state_ = State::Syncing;
  requestUpdate();
}

void WeReadProgressSyncActivity::advanceSync() {
  // Serialize synchronous TLS work with the renderer task and release glyph
  // caches before the handshake, matching the main WeRead activity.
  RenderLock renderBarrier(*this);
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  const auto event = operation_.step();
  switch (event) {
    case WeReadClient::Operation::Event::None:
    case WeReadClient::Operation::Event::Authenticated:
    case WeReadClient::Operation::Event::DetailReady:
    case WeReadClient::Operation::Event::ChapterComplete:
    case WeReadClient::Operation::Event::QrReady:
      return;
    case WeReadClient::Operation::Event::Cancelled:
      returnToReader();
      return;
    case WeReadClient::Operation::Event::Failed:
      error_ = operation_.error();
      state_ = error_ == WeReadClient::Error::SessionExpired ? State::LoginRequired : State::Failed;
      requestUpdate();
      return;
    case WeReadClient::Operation::Event::Complete:
      break;
  }

  const auto result = operation_.progressSyncResult();
  outcome_ = result.outcome;
  LOG_INF("WRSync", "sync complete: outcome=%u", static_cast<unsigned>(outcome_));
  operation_.reset();
  if (outcome_ == WeReadClient::ProgressSyncOutcome::SelectionRequired) {
    remoteFraction_ = result.remote.percent / 100.0f;
    uploadConflict_ = syncMode_ == WeReadClient::ProgressSyncMode::UploadLocal;
    selectedDirection_ = DirectionOption::ApplyRemote;
    syncMode_ = WeReadClient::ProgressSyncMode::Compare;
    state_ = State::ChoosingDirection;
    requestUpdate();
    return;
  }
  if (outcome_ == WeReadClient::ProgressSyncOutcome::ApplyRemote) {
    applyRemoteProgress(result.remote);
    return;
  }
  state_ = State::Success;
  requestUpdate();
}

void WeReadProgressSyncActivity::beginSelectedDirection() {
  switch (selectedDirection_) {
    case DirectionOption::ApplyRemote:
      syncMode_ = WeReadClient::ProgressSyncMode::ApplyRemote;
      break;
    case DirectionOption::UploadLocal:
      syncMode_ = WeReadClient::ProgressSyncMode::UploadLocal;
      break;
  }
  state_ = State::Starting;
  requestUpdate();
}

void WeReadProgressSyncActivity::applyRemoteProgress(const WeReadProtocol::RemoteProgress& remoteProgress) {
  // Mapping needs EPUB metadata after TLS has been released. The object is
  // fallible and activity-scoped; a task-stack Epub is too large.
  auto uniqueEpub = makeUniqueNoThrow<Epub>(epubPath_, "/.crosspoint");
  if (!uniqueEpub) {
    LOG_ERR("WRSync", "OOM: Epub (%u bytes)", static_cast<unsigned>(sizeof(Epub)));
    error_ = WeReadClient::Error::OutOfMemory;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  uniqueEpub->setupCacheDir();
  if (!uniqueEpub->load(false, true)) {
    LOG_ERR("WRSync", "Failed to load EPUB metadata");
    error_ = WeReadClient::Error::Integrity;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  // ProgressMapper currently accepts shared_ptr, but ownership stays here. The
  // aliasing view has no control block and therefore performs no allocation.
  const std::shared_ptr<Epub> epubView(std::shared_ptr<Epub>(), uniqueEpub.get());
  SavedProgressPosition fallback{"", std::max(0.0f, std::min(100.0f, remoteProgress.percent)) / 100.0f};
  bool preciseRemote = false;
  uint32_t remoteTocIndex = 0;
  float chapterFraction = 0.0f;
  float remoteBookFraction = 0.0f;
  int exactSpineIndex = -1;
  WeReadStore::BookOptions options;
  const bool generatedBook = WeReadStore::loadBookOptions(WeReadStore::bookDirectory(bookId_), options);
  const bool canonicalRemote = generatedBook && remoteProgress.hasChapterOffset &&
                               WeReadStore::mapChapterToPosition(
                                   WeReadStore::tocPath(bookId_), remoteProgress.chapterUid,
                                   remoteProgress.chapterOffset, remoteTocIndex, chapterFraction, remoteBookFraction);
  if (canonicalRemote) {
    for (int spine = 0; spine < uniqueEpub->getSpineItemsCount(); ++spine) {
      uint32_t tocIndex = 0;
      if (!WeReadStore::parseGeneratedChapterHref(uniqueEpub->getSpineItem(spine).href, tocIndex) ||
          tocIndex != remoteTocIndex) {
        continue;
      }
      exactSpineIndex = spine;
      preciseRemote = true;
      break;
    }
    if (exactSpineIndex < 0) {
      LOG_ERR("WRSync", "Remote chapter %u is not present in the downloaded range",
              static_cast<unsigned>(remoteTocIndex));
      error_ = WeReadClient::Error::Unavailable;
      state_ = State::Failed;
      requestUpdate();
      return;
    }
  }
  LOG_INF("WRSync", "remote progress mapping: precise=%u toc=%u chapter=%s offset=%u fraction=%lu",
          static_cast<unsigned>(preciseRemote), static_cast<unsigned>(remoteTocIndex), remoteProgress.chapterUid,
          static_cast<unsigned>(remoteProgress.chapterOffset),
          static_cast<unsigned long>(
              (preciseRemote ? uniqueEpub->calculateProgress(exactSpineIndex, chapterFraction) : fallback.percentage) *
                  1000000.0f +
              0.5f));
  const CrossPointPosition target =
      preciseRemote ? ProgressMapper::fromSpineProgress(epubView, exactSpineIndex, chapterFraction, renderer,
                                                        localSpineIndex_, localPageCount_)
                    : ProgressMapper::toCrossPoint(epubView, fallback, renderer, localSpineIndex_, localPageCount_);
  if (target.totalPages <= 0) {
    error_ = WeReadClient::Error::Unavailable;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  std::optional<uint32_t> visibleTextOffset;
  uint32_t resolvedVisibleOffset = 0;
  if (preciseRemote && WeReadXhtmlCodec::nativeToVisibleOffset(
                           WeReadStore::chapterPath(WeReadStore::bookDirectory(bookId_), remoteTocIndex),
                           remoteProgress.chapterOffset, resolvedVisibleOffset)) {
    visibleTextOffset = resolvedVisibleOffset;
  }
  if (!EpubReaderUtils::saveProgress(*uniqueEpub, target.spineIndex, target.pageNumber, target.totalPages,
                                     visibleTextOffset)) {
    error_ = WeReadClient::Error::SdCard;
    state_ = State::Failed;
    requestUpdate();
    return;
  }
  uniqueEpub.reset();
  state_ = State::Success;
  requestUpdate();
}

void WeReadProgressSyncActivity::returnToReader() { activityManager.goToReader(epubPath_); }

const char* WeReadProgressSyncActivity::resultMessage() const {
  switch (outcome_) {
    case WeReadClient::ProgressSyncOutcome::Pending:
    case WeReadClient::ProgressSyncOutcome::AlreadySynced:
      return tr(STR_ALREADY_SYNCED);
    case WeReadClient::ProgressSyncOutcome::SelectionRequired:
      return tr(STR_WEREAD_PROGRESS_CHANGED);
    case WeReadClient::ProgressSyncOutcome::ApplyRemote:
      return tr(STR_WEREAD_REMOTE_PROGRESS_APPLIED);
    case WeReadClient::ProgressSyncOutcome::LocalUploaded:
      return tr(STR_WEREAD_LOCAL_PROGRESS_UPLOADED);
  }
  return tr(STR_ALREADY_SYNCED);
}

const char* WeReadProgressSyncActivity::errorMessage() const {
  switch (error_) {
    case WeReadClient::Error::Unavailable:
      return tr(STR_WEREAD_PROGRESS_UNAVAILABLE);
    case WeReadClient::Error::Network:
      return tr(STR_WEREAD_HTTP_ERROR);
    case WeReadClient::Error::SdCard:
      return tr(STR_SAVE_PROGRESS_FAILED);
    case WeReadClient::Error::Clock:
      return tr(STR_CLOCK_SYNC_FAIL);
    case WeReadClient::Error::SessionExpired:
      return tr(STR_WEREAD_LOGIN_REQUIRED);
    case WeReadClient::Error::Ok:
    case WeReadClient::Error::Cancelled:
    case WeReadClient::Error::LoginFailed:
    case WeReadClient::Error::Protocol:
    case WeReadClient::Error::Integrity:
    case WeReadClient::Error::OutOfMemory:
      return tr(STR_SYNC_FAILED_MSG);
  }
  return tr(STR_SYNC_FAILED_MSG);
}

void WeReadProgressSyncActivity::loop() {
  switch (state_) {
    case State::WifiSelection:
      return;
    case State::Starting:
      startSync();
      return;
    case State::Syncing:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        operation_.cancel();
      }
      advanceSync();
      return;
    case State::ChoosingDirection: {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        returnToReader();
        return;
      }
      const auto& metrics = UITheme::getInstance().getMetrics();
      const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const Rect content = SubpageLayout::contentRect(screen, metrics);
      const int optionStep = metrics.menuRowHeight + metrics.menuSpacing;
      const int optionTop = content.y + content.height - optionStep * 2;
      int touchedOption = -1;
      const auto touch = mappedInput.rowTouch(touchedOption, optionTop, optionStep, 2, content.x,
                                              content.x + content.width, metrics.menuRowHeight);
      if (touch != MappedInputManager::RowTouch::None) {
        selectedDirection_ = touchedOption == 0 ? DirectionOption::ApplyRemote : DirectionOption::UploadLocal;
        if (touch == MappedInputManager::RowTouch::Tap) {
          beginSelectedDirection();
        } else {
          requestUpdate();
        }
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) ||
          mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
        selectedDirection_ = selectedDirection_ == DirectionOption::ApplyRemote ? DirectionOption::UploadLocal
                                                                                : DirectionOption::ApplyRemote;
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        beginSelectedDirection();
      }
      return;
    }
    case State::Success:
    case State::LoginRequired: {
      int x = 0;
      int y = 0;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
          mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
        returnToReader();
      }
      return;
    }
    case State::Failed:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        returnToReader();
        return;
      }
      {
        int x = 0;
        int y = 0;
        if ((mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) &&
            (error_ == WeReadClient::Error::Network || error_ == WeReadClient::Error::Clock ||
             error_ == WeReadClient::Error::Unavailable)) {
          state_ = State::Starting;
          requestUpdate();
        }
        return;
      }
  }
}

void WeReadProgressSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_WEREAD_SYNC_PROGRESS));
  const Rect content = SubpageLayout::contentRect(screen, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);

  switch (state_) {
    case State::WifiSelection:
    case State::Starting:
    case State::Syncing:
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_WEREAD_SYNCING_PROGRESS), true, EpdFontFamily::BOLD);
      break;
    case State::ChoosingDirection: {
      const int contentTop = content.y + SubpageLayout::sectionGap(metrics);
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const char* comparisonTitle = uploadConflict_ ? tr(STR_WEREAD_PROGRESS_CHANGED) : tr(STR_PROGRESS_FOUND);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, contentTop, comparisonTitle, true,
                                EpdFontFamily::BOLD);

      char remoteValue[24];
      snprintf(remoteValue, sizeof(remoteValue), "%.2f%%", remoteFraction_ * 100.0f);
      char localValue[64];
      snprintf(localValue, sizeof(localValue), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), localPageNumber_ + 1, localPageCount_,
               input_.localFraction * 100.0f);
      const int remoteLabelY = contentTop + lineHeight * 2;
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, remoteLabelY, tr(STR_REMOTE_LABEL), true,
                                EpdFontFamily::BOLD);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, remoteLabelY + lineHeight, remoteValue);
      const int localLabelY = remoteLabelY + lineHeight * 3;
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, localLabelY, tr(STR_LOCAL_LABEL), true,
                                EpdFontFamily::BOLD);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, localLabelY + lineHeight, localValue);

      const int optionStep = metrics.menuRowHeight + metrics.menuSpacing;
      const int optionTop = content.y + content.height - optionStep * 2;
      const int optionTextOffset = (metrics.menuRowHeight - lineHeight) / 2;
      const DirectionOption options[] = {DirectionOption::ApplyRemote, DirectionOption::UploadLocal};
      for (int index = 0; index < 2; ++index) {
        const int optionY = optionTop + optionStep * index;
        const bool selected = selectedDirection_ == options[index];
        if (selected) {
          renderer.fillRect(textBounds.x, optionY, textBounds.width, metrics.menuRowHeight);
        }
        const char* label =
            options[index] == DirectionOption::ApplyRemote ? tr(STR_APPLY_REMOTE) : tr(STR_UPLOAD_LOCAL);
        UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, optionY + optionTextOffset, label, !selected);
      }
      break;
    }
    case State::Success:
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                resultMessage(), true, EpdFontFamily::BOLD);
      break;
    case State::LoginRequired:
      UITheme::drawCenteredWrappedText(renderer, textBounds, UI_10_FONT_ID, tr(STR_WEREAD_LOGIN_REQUIRED), 2, true,
                                       EpdFontFamily::BOLD);
      break;
    case State::Failed:
      UITheme::drawCenteredWrappedText(renderer, textBounds, UI_10_FONT_ID, errorMessage(), 2, true,
                                       EpdFontFamily::BOLD);
      break;
  }

  if (state_ == State::ChoosingDirection) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == State::Success || state_ == State::LoginRequired || state_ == State::Failed) {
    const bool retryable =
        state_ == State::Failed && (error_ == WeReadClient::Error::Network || error_ == WeReadClient::Error::Clock ||
                                    error_ == WeReadClient::Error::Unavailable);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", retryable ? tr(STR_RETRY) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

#endif
