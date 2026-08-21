#pragma once

#include <Txt.h>
#include <TxtEncoding.h>

#include <array>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"

class TxtReaderActivity final : public ReaderActivity {
  enum class PageMode : uint8_t { Indexed, Direct };

  struct TxtLine {
    std::string text;
    bool indented = false;
  };
  static_assert(sizeof(TxtLine) <= sizeof(std::string) + alignof(std::string));

  static constexpr size_t DIRECT_PAGE_HISTORY_SIZE = 32;

  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  unsigned long openStartMs = 0;
  bool firstPageLogged = false;
  bool endOfBook = false;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  // Chapter jumps use a bounded local history instead of paginating from the
  // start of the book. On ESP32 this adds a fixed 128 bytes to the Activity.
  std::array<size_t, DIRECT_PAGE_HISTORY_SIZE> directPageOffsets{};
  size_t directPageIndex = 0;
  size_t directPageCount = 0;
  int directReturnPage = 0;
  PageMode pageMode = PageMode::Indexed;
  std::vector<TxtLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  bool indexComplete = false;
  bool indexCacheDirty = false;
  size_t currentPageEndOffset = 0;
#ifdef ENABLE_CHINESE_VERSION
  txt_encoding::Encoding textEncoding = txt_encoding::Encoding::Unknown;
#else
  txt_encoding::Encoding textEncoding = txt_encoding::Encoding::Utf8;
#endif

  // Reused for the Activity lifetime: 8KB is too large for the render task's 8KB stack.
  std::unique_ptr<uint8_t[]> pageBuffer;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int paragraphIndentWidth = 0;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  void probeTextEncoding();
  bool loadPageAtOffset(size_t offset, std::vector<TxtLine>& outLines, size_t& nextOffset);
  bool advancePageIndex(size_t nextOffset);
  bool extendIndexToPage(size_t targetPage);
  void goToSourceOffset(size_t sourceOffset, int returnPage);
  size_t getCurrentSourceOffset() const;
  int getDisplayPageNumber() const;
  void openChapterSelection();
  void updateTotalPages();
  int getProgressPercent() const;
  bool loadPageIndexCache();
  bool savePageIndexCache();
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return txt ? txt->getTitle() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("TxtReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override { return endOfBook; }
  void onReturnFromEndOfBook() override { endOfBook = false; }
  ScreenshotInfo getScreenshotInfo() const override;
};
