#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "BookStyleStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "XtcReaderActivity.h"
#include "util/PluginEvents.h"

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::applyBookStyle() {
  // Snapshot the global settings once on enter, for every book and whether or
  // not memory is on: enabling memory from the text settings later in the
  // session needs a baseline, and a baseline that only exists for some
  // sessions is what made that toggle a no-op (applyBookStyleMemoryToggle()).
  if (!globalSettingsSnapshotted_) {
    globalSettingsSnapshot_ = captureStyleFromSettings();
    globalSettingsSnapshotted_ = true;
  }
  // The per-book overrides can be turned off from the text settings; then every
  // book opens with the global settings, exactly like the stock firmware, and
  // the baseline above is only used to keep ordinary typography edits global.
  if (!SETTINGS.bookStyleMemory) return;
  BookStyle style;
  // Books without their own entry keep the global settings from the settings
  // screen.
  if (!BOOK_STYLES.findStyle(bookPath, style)) return;
  applyStyleOverrides(style);
}

void ReaderActivity::saveBookStyle() {
  // Override model: per-book values are captured by the text-settings entry,
  // which persists the store itself; exiting a book no longer snapshots the
  // current settings into the book. Only restore the global snapshot so the
  // in-memory overrides never leak into the next book.
  if (!globalSettingsSnapshotted_) {
    // No book session ever started here, so SETTINGS only holds global values.
    // Persist them: an in-place edit from a child screen (night mode,
    // frontlight in the reader menu) may have no other save point when the
    // device sleeps straight out of that screen.
    SETTINGS.saveToFile();
    return;
  }
  // Roll back to the entered baseline only while memory is on: SETTINGS may then
  // hold this book's overrides, and writing them would put the book's font into
  // the global settings file.
  //
  // With memory switched off - either it was off when the book opened, or the
  // user switched it off mid-session (the switch-off handler captured the book's
  // edits and put the global style back) - the live values ARE the user's global
  // baseline, and edits made afterwards are deliberately global, so they are
  // what belongs in the file and must not be rolled back.
  if (SETTINGS.bookStyleMemory) applyStyleToSettings(globalSettingsSnapshot_);
  SETTINGS.saveToFile();
  globalSettingsSnapshotted_ = false;
}

BookStyle ReaderActivity::captureStyleDiff(const BookStyle& globalSnapshot) const {
  return diffStyleFromSettings(globalSnapshot);
}

bool ReaderActivity::persistBookStyleEdits() {
  if (!SETTINGS.bookStyleMemory || !globalSettingsSnapshotted_) return false;
  const BookStyle overrides = captureStyleDiff(globalSettingsSnapshot_);
  if (overrides.overrideMask != 0) {
    BOOK_STYLES.updateStyle(bookPath, overrides);
  } else {
    // Everything was reverted to the global values: forget the entry so the
    // book follows the global settings again.
    BOOK_STYLES.removeByPath(bookPath);
  }
  // Put the entered global values back, persist those, then re-apply the
  // overrides in memory: the edits live in the book-style store only.
  applyStyleToSettings(globalSettingsSnapshot_);
  SETTINGS.saveToFile();
  if (overrides.overrideMask != 0) applyStyleOverrides(overrides);
  return true;
}

void ReaderActivity::saveSettingsKeepingBookStyle() {
  // Shared contract (BookStyleStore.h): with a snapshot and memory on, the file
  // gets the entered global typography and the session keeps the book's.
  saveGlobalSettingsKeepingBookStyle(globalSettingsSnapshotted_ ? &globalSettingsSnapshot_ : nullptr);
}

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::notePageTurn(const bool forward, const bool succeeded) {
  RenderLock lock(*this);
  readerSession.noteTurn(forward, succeeded);
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  // Restore this book's own remembered style before the font system and the
  // render spec are built, so the book opens exactly as it was left. Books
  // without a remembered style simply keep the global settings.
  applyBookStyle();

  // Clear remembered book after opening it
  if (!APP_STATE.openEpubPath.empty()) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }

  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    if (!handleLoadFailure()) finish();
    return;
  }

  requestUpdate();
}

void ReaderActivity::rememberBookOnceRendered() {
  if (bookRemembered || !pageRendered.load(std::memory_order_acquire)) return;
  bookRemembered = true;
  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  const pluginevents::Var openVars[] = {{"book", bookPath.c_str()}};
  pluginevents::emit(pluginevents::Event::ReaderOpen, openVars, 1);
}

void ReaderActivity::onExit() {
  // Per-book overrides are captured and persisted by the text-settings entry;
  // here we only restore the global snapshot so the in-memory overrides never
  // leak into the next book.
  saveBookStyle();
  // saveBookStyle() restored the global settings (incl. font family). Re-sync
  // the loaded font cache with the restored values so an SD-card font applied
  // by this book's style (which also registers UI fallbacks) is unloaded again;
  // otherwise the UI would keep rendering with the book's font after exit.
  // Note: onExit() is always invoked by ActivityManager while it already holds
  // the rendering mutex (exitActivity()'s contract: "lock must be held by the
  // caller"). Taking RenderLock again here would self-deadlock on the
  // non-recursive mutex — the stock firmware's onExit() has no lock for the
  // same reason. ensureLoaded() is safe lock-free here: it runs on the UI
  // thread with the render task quiesced behind the held mutex, matching the
  // lock-free ensureLoaded() in onEnter().
  sdFontSystem.ensureLoaded(renderer);

  Activity::onExit();

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // Flush BEFORE the ReaderExit event: the session's final progress must be
  // durable before a subscriber can act on the exit notification.
  flushReaderSession();

  if (pluginevents::anySubscriber(pluginevents::Event::ReaderExit)) {
    char percent[8];
    snprintf(percent, sizeof(percent), "%d", getScreenshotInfo().progressPercent);
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()}, {"percent", percent}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 2);
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::prepareForSleep() { flushReaderSession(); }

void ReaderActivity::flushReaderSession() {
  if (!readerSession.isEmitWorthy() || !pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    readerSession.reset();
    return;
  }

  const std::string document = KOReaderDocumentId::calculate(bookPath);
  const bool validDocument =
      document.size() == 32 && std::all_of(document.begin(), document.end(), [](const unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
      });
  if (validDocument) {
    char startTime[24];
    char endTime[24];
    char duration[16];
    char startProgress[8];
    char endProgress[8];
    snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(readerSession.startTime()));
    snprintf(endTime, sizeof(endTime), "%lld", static_cast<long long>(readerSession.endTime()));
    snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(readerSession.durationSeconds()));
    snprintf(startProgress, sizeof(startProgress), "%u", readerSession.startProgressBp());
    snprintf(endProgress, sizeof(endProgress), "%u", readerSession.endProgressBp());
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()},       {"document", document.c_str()},
                                      {"start_time", startTime},        {"end_time", endTime},
                                      {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                      {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
    pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
  }
  readerSession.reset();
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::loop() {
  rememberBookOnceRendered();
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  // Zone actions dispatch on the release frame. A long-press action wins over
  // the short action; an unhandled action (not supported by this reader type)
  // is swallowed so it never falls through to a page turn. Bookmark/dictionary
  // are EPUB-only: the base class leaves those unhandled and the
  // touch.bookmark/touch.dictionary check below keeps the original swallow.
  if (touch.longPress && touch.longAction != CrossPointSettings::TAP_ZONE_LONG_NONE &&
      handleZoneLongAction(touch.longAction)) {
    requestUpdate();
    return;
  }
  if (!touch.longPress && touch.action != CrossPointSettings::TAP_ZONE_NONE && handleZoneShortAction(touch.action)) {
    requestUpdate();
    return;
  }
  if (touch.bookmark || touch.dictionary) return;
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  const bool changed = skip ? skipPages(prevTriggered ? -10 : 10) : pageTurn(!prevTriggered);
  // A skip is navigation, not reading: it never counts toward session dwell.
  notePageTurn(!skip && !prevTriggered, changed);
  if (changed && (touch.prev || touch.next)) haptic_feedback::touchAction(skip);
  requestUpdate();
}

bool ReaderActivity::handleZoneShortAction(const uint8_t action) {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_ROTATE_CW:
      rotateOrientation(1);
      return true;
    case CrossPointSettings::TAP_ZONE_ROTATE_CCW:
      rotateOrientation(-1);
      return true;
    case CrossPointSettings::TAP_ZONE_FRONTLIGHT:
      toggleFrontlight();
      return true;
    case CrossPointSettings::TAP_ZONE_GO_HOME:
      onGoHome();
      return true;
    case CrossPointSettings::TAP_ZONE_FOOTNOTE:
      // Readers without footnotes (TXT / PDF / XTC) swallow the action so it
      // never falls through to page turning; EpubReaderActivity overrides it.
      return true;
    case CrossPointSettings::TAP_ZONE_REFRESH:
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      requestUpdate();
      return true;
    default:
      return false;
  }
}

bool ReaderActivity::handleZoneLongAction(const uint8_t action) {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_LONG_ROTATE_CW:
      rotateOrientation(1);
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_ROTATE_CCW:
      rotateOrientation(-1);
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_FRONTLIGHT:
      toggleFrontlight();
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_GO_HOME:
      onGoHome();
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_FOOTNOTE:
      return true;
    case CrossPointSettings::TAP_ZONE_LONG_REFRESH:
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      requestUpdate();
      return true;
    default:
      return false;
  }
}

void ReaderActivity::rotateOrientation(const int delta) {
  const uint8_t next = static_cast<uint8_t>((SETTINGS.orientation + delta + CrossPointSettings::ORIENTATION_COUNT) %
                                            CrossPointSettings::ORIENTATION_COUNT);
  if (next == SETTINGS.orientation) return;
  SETTINGS.orientation = next;
  SETTINGS.saveToFile();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
}

void ReaderActivity::toggleFrontlight() {
  if (!Frontlight.present()) return;
  const bool on = !Frontlight.isOn();
  Frontlight.setOn(on);
  SETTINGS.frontlightOn = on ? 1 : 0;
  SETTINGS.saveToFile();
}

void ReaderActivity::render(RenderLock&&) {
  renderCommitted = false;
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    if (renderer.displayCommitted()) {
      onEndOfBookRendered();
      markPageRendered();
    }
  } else {
    renderBook();
  }
  if (renderCommitted) {
    readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
  } else if (activityManager.isSwitchPending()) {
    readerSession.noteTurn(false, false);
  }
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}
