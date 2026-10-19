#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
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

namespace {

BookStyle snapshotStyleFromSettings() {
  BookStyle style;
  style.fontFamily = SETTINGS.fontFamily;
  memcpy(style.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(style.sdFontFamilyName));
  style.fontPointSize = SETTINGS.fontPointSize;
  style.lineSpacing = SETTINGS.lineSpacing;
  style.paragraphAlignment = SETTINGS.paragraphAlignment;
  style.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  style.firstLineIndent = SETTINGS.firstLineIndent;
  style.paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  style.fakeBold = SETTINGS.fakeBold;
  style.textAntiAliasing = SETTINGS.textAntiAliasing;
  style.wordSpacing = SETTINGS.wordSpacing;
  style.characterSpacing = SETTINGS.characterSpacing;
  style.screenMargin = SETTINGS.screenMargin;
  style.embeddedStyle = SETTINGS.embeddedStyle;
  style.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  style.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  style.readingGuideLineEnabled = SETTINGS.readingGuideLineEnabled;
  style.readingGuideLineStyle = SETTINGS.readingGuideLineStyle;
  style.readingGuideLineOffset = SETTINGS.readingGuideLineOffset;
  return style;
}

}  // namespace

void ReaderActivity::applyBookStyle() {
  // The per-book overrides can be turned off from the text settings; then every
  // book opens with the global settings, exactly like the stock firmware.
  // When it was off before this book opened, take no snapshot either, so
  // ordinary typography edits keep the stock behaviour and persist as-is.
  if (!SETTINGS.bookStyleMemory) return;
  // Snapshot the global settings once on enter, for every book, so overrides
  // edited while reading can be restored on exit and never leak into the next
  // book — even when per-book memory is turned off mid-session.
  if (!globalSettingsSnapshotted_) {
    globalSettingsSnapshot_ = snapshotStyleFromSettings();
    globalSettingsSnapshotted_ = true;
  }
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
    // No snapshot means memory was off when this book opened, so SETTINGS only
    // ever held the user's global values. Persist them: an in-place edit from a
    // child screen (night mode, frontlight in the reader menu) may have no
    // other save point when the device sleeps straight out of that screen.
    SETTINGS.saveToFile();
    return;
  }
  if (SETTINGS.bookStyleMemory) {
    // Memory is still on: roll back to the entered global snapshot so a
    // book's overrides never leak into the next book or the settings file.
    applyStyleToSettings(globalSettingsSnapshot_);
  }
  // Memory was switched off mid-session: the switch-off handler already
  // restored the snapshot, so the currently displayed values are the user's
  // new global baseline — keep them and persist instead of rolling back
  // edits made after the switch was turned off.
  SETTINGS.saveToFile();
  globalSettingsSnapshotted_ = false;
}

BookStyle ReaderActivity::captureStyleDiff(const BookStyle& globalSnapshot) const {
  // A field the user changed in the text settings differs from the entered
  // snapshot and gets its bit set; a field reverted to the global value clears
  // its bit ("follow global"). Fields already overridden for this book differ
  // from the snapshot too, so existing overrides are preserved by the same
  // comparison.
  BookStyle overrides;
  uint32_t& m = overrides.overrideMask;
  if (SETTINGS.fontFamily != globalSnapshot.fontFamily) {
    m |= BookStyle::kFontFamily;
    overrides.fontFamily = SETTINGS.fontFamily;
  }
  if (strcmp(SETTINGS.sdFontFamilyName, globalSnapshot.sdFontFamilyName) != 0) {
    m |= BookStyle::kSdFontFamily;
    memcpy(overrides.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(overrides.sdFontFamilyName));
  }
  if (SETTINGS.fontPointSize != globalSnapshot.fontPointSize) {
    m |= BookStyle::kFontPointSize;
    overrides.fontPointSize = SETTINGS.fontPointSize;
  }
  if (SETTINGS.lineSpacing != globalSnapshot.lineSpacing) {
    m |= BookStyle::kLineSpacing;
    overrides.lineSpacing = SETTINGS.lineSpacing;
  }
  if (SETTINGS.paragraphAlignment != globalSnapshot.paragraphAlignment) {
    m |= BookStyle::kParagraphAlignment;
    overrides.paragraphAlignment = SETTINGS.paragraphAlignment;
  }
  if (SETTINGS.extraParagraphSpacing != globalSnapshot.extraParagraphSpacing) {
    m |= BookStyle::kExtraParagraphSpacing;
    overrides.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  }
  if (SETTINGS.firstLineIndent != globalSnapshot.firstLineIndent) {
    m |= BookStyle::kFirstLineIndent;
    overrides.firstLineIndent = SETTINGS.firstLineIndent;
  }
  if (SETTINGS.paragraphIndentSpaces != globalSnapshot.paragraphIndentSpaces) {
    m |= BookStyle::kParagraphIndentSpaces;
    overrides.paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  }
  if (SETTINGS.fakeBold != globalSnapshot.fakeBold) {
    m |= BookStyle::kFakeBold;
    overrides.fakeBold = SETTINGS.fakeBold;
  }
  if (SETTINGS.textAntiAliasing != globalSnapshot.textAntiAliasing) {
    m |= BookStyle::kTextAntiAliasing;
    overrides.textAntiAliasing = SETTINGS.textAntiAliasing;
  }
  if (SETTINGS.wordSpacing != globalSnapshot.wordSpacing) {
    m |= BookStyle::kWordSpacing;
    overrides.wordSpacing = SETTINGS.wordSpacing;
  }
  if (SETTINGS.characterSpacing != globalSnapshot.characterSpacing) {
    m |= BookStyle::kCharacterSpacing;
    overrides.characterSpacing = SETTINGS.characterSpacing;
  }
  if (SETTINGS.screenMargin != globalSnapshot.screenMargin) {
    m |= BookStyle::kScreenMargin;
    overrides.screenMargin = SETTINGS.screenMargin;
  }
  if (SETTINGS.embeddedStyle != globalSnapshot.embeddedStyle) {
    m |= BookStyle::kEmbeddedStyle;
    overrides.embeddedStyle = SETTINGS.embeddedStyle;
  }
  if (SETTINGS.focusReadingEnabled != globalSnapshot.focusReadingEnabled) {
    m |= BookStyle::kFocusReading;
    overrides.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  }
  if (SETTINGS.hyphenationEnabled != globalSnapshot.hyphenationEnabled) {
    m |= BookStyle::kHyphenation;
    overrides.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  }
  if (SETTINGS.readingGuideLineEnabled != globalSnapshot.readingGuideLineEnabled) {
    m |= BookStyle::kGuideLine;
    overrides.readingGuideLineEnabled = SETTINGS.readingGuideLineEnabled;
  }
  if (SETTINGS.readingGuideLineStyle != globalSnapshot.readingGuideLineStyle) {
    m |= BookStyle::kGuideLineStyle;
    overrides.readingGuideLineStyle = SETTINGS.readingGuideLineStyle;
  }
  if (SETTINGS.readingGuideLineOffset != globalSnapshot.readingGuideLineOffset) {
    m |= BookStyle::kGuideLineOffset;
    overrides.readingGuideLineOffset = SETTINGS.readingGuideLineOffset;
  }
  return overrides;
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
  // Only a live snapshot plus memory on can leave overrides in SETTINGS;
  // without one, SETTINGS already holds the user's global values.
  if (!globalSettingsSnapshotted_ || !SETTINGS.bookStyleMemory) {
    SETTINGS.saveToFile();
    return;
  }
  // The write must carry the entered global typography, but the session must
  // keep rendering with this book's overrides. Swap the snapshot in for the
  // write and put the live style back afterwards.
  const BookStyle liveStyle = snapshotStyleFromSettings();
  applyStyleToSettings(globalSettingsSnapshot_);
  SETTINGS.saveToFile();
  applyStyleToSettings(liveStyle);
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
