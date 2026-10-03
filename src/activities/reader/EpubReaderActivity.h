#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ChapterPosition.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderFontPreview.h"
#include "ReaderPageCache.h"
#include "ReaderToolbarUi.h"
#include "components/OptionPopup.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  uint8_t pageTurnRate = 15;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  bool pendingSyncLaunchError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;

#if FREEINK_DEVICE_READPICO
  // Two slots of four reusable PSRAM frames each (~812 KiB total); too large for
  // the render task's stack. The activity owns them and releases them in onExit().
  //
  // Two slots because the cache only pays off for the page the reader actually turns
  // to, and a reader turns both ways. With one slot the build could only ever cover
  // currentPage + 1, so every backward turn was a guaranteed miss -- measured: a
  // session that alternated directions hit once in eight turns, while one that mostly
  // advanced hit four in six. Slot 0 is the forward page (+1), slot 1 the backward
  // one (-1); a human reading pace leaves room to build both (each takes ~700 ms).
  static constexpr int kPageCacheSlots = 2;
  memory::ByteBuffer pageCacheBase_[kPageCacheSlots];
  memory::ByteBuffer pageCacheLsb_[kPageCacheSlots];
  memory::ByteBuffer pageCacheMsb_[kPageCacheSlots];
  memory::ByteBuffer pageCacheStash_[kPageCacheSlots];
  ReaderPageCache pageCache_[kPageCacheSlots];
  // Which slot the hit test matched, so the three plane copies in renderContents()
  // read the same slot the test looked at rather than assuming slot 0.
  int pageCacheLiveSlot_ = 0;
  ReaderPageCacheKey renderedPageKey_;
  uint32_t sectionGeneration_ = 0;
  uint32_t renderEpoch_ = 0;
  bool pageCacheFailed_ = false;
#ifdef ENABLE_CHINESE_VERSION
  uint32_t pageCacheMissingCodepoint_[kPageCacheSlots] = {};
#endif

  bool pageCacheEligible() const;
  ReaderPageCacheKey pageCacheKey(int page, int top, int right, int bottom, int left) const;
  uint32_t idleRenderDelayMs() const override;
  void renderIdle(uint32_t generation) override;
  // Build one page into one cache slot; only called from renderIdle().
  bool buildPageCacheSlot(int slot, const ReaderPageCacheKey& key, uint32_t generation);
  void freePageCache();
#endif

  bool bookmarkRemoved = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool recentsEntryRemoved = false;
  unsigned long bookmarkMessageTime = 0UL;
  bool pendingReadFolderMove = false;

#ifdef ENABLE_CHINESE_VERSION
  std::atomic<uint32_t> pendingMissingChineseCodepoint_{0};
  char wereadBookId_[64] = {};
  bool clearInitialProgressAfterSave_ = false;
  bool maybeOfferCompleteChineseFont();
#endif

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup{true};
  ReaderFontPreview fontPreview;
  enum class FontPromptState { Idle, Asking, Accepted, TooLarge };
  FontPromptState fontPromptState = FontPromptState::Idle;
  unsigned long fontNoticeStartedAt = 0;
  bool fontPromptWaitForBackRelease = false;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  // True while a deferred overlay chrome refresh (pushOverlayRefresh) may still
  // be running on the panel. settleOverlayRefresh() must run before the
  // framebuffer is touched or another differential refresh is pushed.
  bool overlayRefreshPending = false;
  void pushOverlayRefresh();
  void settleOverlayRefresh();
  // The More panel's image-scaling row is a one-tap toggle that deliberately
  // does NOT re-render the page: the overlay page snapshot was decoded with the
  // old filter, so it is dropped on close and the page re-decodes once there.
  bool imageScalingDirty = false;
  int autoTurnOption = 0;  // current auto page-turn rate index (More panel)
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  // Footnote support
  FootnoteList currentPageFootnotes;
  PageLinkList currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
    // Fixed-size return stack; offsets survive session CSS fallback without heap storage.
    std::optional<uint32_t> visibleTextOffset;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  // The back-stack outlives the reader (sleep, home) in links.bin so Back
  // still returns to where a followed link was tapped.
  void saveLinkStack() const;
  void loadLinkStack();

  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  // Requires the render lock; heap admission is checked separately by the build tick.
  bool backgroundBuildWanted() const;
  bool buildTickHeapGate();
  void prepareChapterBuild();
  bool needsPartialRebuild() const;
  bool updateChapterBuild();
  bool stylesDisabledForSession_ = false;
  int renderedSpineIndex_ = -1;
  int failedBuildSpine_ = -1;
  ReaderRenderSpec effectiveRenderSpec(uint16_t width, uint16_t height) const;
  bool handleBuildFailure(const char* stage, Section::BuildError error);
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr size_t RENDER_MIN_MAX_ALLOC = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  bool jumpToFraction(float fraction);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  int currentTocIndex() const;
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, first-line indent).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  void finishFontPreview();
  void handleFontPreloadPrompt();

  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
  void openFootnoteSelect(bool reopenMenuOnCancel);
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
#ifdef ENABLE_CHINESE_VERSION
  bool launchWeReadSync();
#endif
  unsigned long confirmLongPressThreshold() const;
  void toggleAutoPageTurn(uint8_t requestedPageTurnRate);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  // Modal shown when a protected book refuses to open (loan expired /
  // date unverified); OK exits, "Sync time" (when offered) verifies the
  // clock over Wi-Fi and reopens the book.
  OptionPopup loadFailurePopup;
  // Protection error captured by loadBook() for handleLoadFailure(); the
  // failed Epub itself does not outlive loadBook().
  std::string loadProtectionError;
  bool legacyProgressPending = false;

  bool loadBook() override;
  bool handleLoadFailure() override;
  // Wi-Fi join + SNTP for a loan whose date could not be verified, then a
  // clean re-open of the book.
  void beginLoanTimeSync();
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  int getProgressBasisPoints() const override;
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;

  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;
  bool preventAutoSleep() override { return automaticPageTurnActive; }
  bool deferBluetoothStart() const override;

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
