#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <TxtPageIndex.h>
#include <Utf8.h>

#include <cassert>
#include <cstring>
#include <limits>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;       // 8KB chunk for reading
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"

template <typename T>
bool readPodChecked(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePodChecked(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}
}  // namespace

void TxtReaderActivity::onEnter() {
  Activity::onEnter();

  if (!txt) {
    return;
  }

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  txt->setupCacheDir();

  // Allocated once and reused; putting 8193 bytes on the render task's 8KB stack would overflow it.
  pageBuffer = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE + 1);
  if (!pageBuffer) {
    LOG_ERR("TRS", "OOM: TXT page buffer (%u bytes)", static_cast<unsigned>(CHUNK_SIZE + 1));
  }

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");
  READING_STATS.beginSession(filePath, fileName, "", "", 0, "", 0);

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  Activity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (indexCacheDirty) {
    savePageIndexCache();
  }
  pageOffsets.clear();
  currentPageLines.clear();
  pageBuffer.reset();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  showPendingAchievementPopups(renderer);
  txt.reset();
}

void TxtReaderActivity::loop() {
  READING_STATS.tickActiveSession();

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, txt ? txt->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<TxtReaderActivity*>(ctx)->onGoHome(); }})) {
    return;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  READING_STATS.noteActivity();

  bool pageChanged = false;
  bool reachedEnd = false;
  {
    RenderLock lock(*this);
    if (prevTriggered && currentPage > 0) {
      currentPage--;
      pageChanged = true;
    } else if (nextTriggered) {
      if (static_cast<size_t>(currentPage + 1) < pageOffsets.size()) {
        currentPage++;
        pageChanged = true;
      } else if (indexComplete) {
        reachedEnd = true;
      }
    }
  }

  if (pageChanged) {
    requestUpdate();
  } else if (reachedEnd) {
    onGoHome();
  }
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  if (!pageBuffer) {
    initialized = true;
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  renderer.getOrientedViewableTRBL(&cachedOrientedMarginTop, &cachedOrientedMarginRight, &cachedOrientedMarginBottom,
                                   &cachedOrientedMarginLeft);
  cachedOrientedMarginTop += cachedScreenMargin;
  cachedOrientedMarginLeft += cachedScreenMargin;
  cachedOrientedMarginRight += cachedScreenMargin;
  cachedOrientedMarginBottom +=
      std::max(cachedScreenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
  currentPageLines.reserve(linesPerPage);

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  if (!loadPageIndexCache()) {
    pageOffsets.clear();
    pageOffsets.reserve(txt_page_index::CHECKPOINT_PAGE_COUNT);
    if (txt->getFileSize() > 0) {
      pageOffsets.push_back(0);
    }
    indexComplete = txt->getFileSize() == 0;
    indexCacheDirty = true;
    updateTotalPages();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (!pageBuffer || offset >= fileSize) {
    return false;
  }

  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* const buffer = pageBuffer.get();

  if (!txt->readContent(buffer, offset, chunkSize)) {
    return false;
  }
  buffer[chunkSize] = '\0';

  // Leave an incomplete UTF-8 sequence at a non-final chunk boundary for the
  // next read instead of measuring or indexing a partial codepoint.
  if (offset + chunkSize < fileSize) {
    chunkSize =
        static_cast<size_t>(utf8SafeTruncateBuffer(reinterpret_cast<const char*>(buffer), static_cast<int>(chunkSize)));
    buffer[chunkSize] = '\0';
  }

  // Prime the SD card font's advance table with this chunk's codepoints.
  // Without this, every getTextAdvanceX() call in the wrap loop below triggers
  // on-demand glyph loads through the 8-slot overflow ring buffer, which
  // thrashes for any text with more than 8 unique chars (i.e. all English),
  // floods the heap with short-lived bitmap allocations, and eventually
  // corrupts FreeRTOS state. The advance table persists across calls per
  // font, so the cost amortizes to ~ASCII-size after the first chunk.
  if (renderer.isSdCardFont(cachedFontId)) {
    renderer.ensureSdCardFontReady(cachedFontId, reinterpret_cast<const char*>(buffer), /*styleMask=*/0x01);
  }

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }
    const bool hasNewline = lineEnd < chunkSize;

    // Check if we have a complete line
    const bool lineComplete = hasNewline || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    if (displayLen == 0) {
      // Emit one visual line for an empty source line.
      outLines.emplace_back();
    } else {
      char* const line = reinterpret_cast<char*>(buffer + pos);
      const char lineTerminator = line[displayLen];
      line[displayLen] = '\0';

      while (lineBytePos < displayLen && static_cast<int>(outLines.size()) < linesPerPage) {
        size_t scanPos = lineBytePos;
        size_t lastFittingPos = lineBytePos;
        size_t lastFittingSpace = std::string::npos;
        int cjkWidth = 0;
        bool cjkFastPath = true;
        bool overflowed = false;

        while (scanPos < displayLen) {
          const size_t codepointStart = scanPos;
          const auto* codepointPtr = reinterpret_cast<const unsigned char*>(line + scanPos);
          const uint32_t codepoint = utf8NextCodepoint(&codepointPtr);
          size_t codepointEnd = static_cast<size_t>(reinterpret_cast<const char*>(codepointPtr) - line);

          // Embedded NUL is not valid TXT content, but still consume it as one
          // byte so malformed input cannot stall pagination.
          if (codepointEnd <= codepointStart) {
            codepointEnd = codepointStart + 1;
          }
          if (codepointEnd > displayLen) {
            codepointEnd = displayLen;
          }

          int candidateWidth;
          if (cjkFastPath && (utf8IsCjkBreakable(codepoint) || utf8IsCombiningMark(codepoint))) {
            char codepointText[5];
            const size_t codepointBytes = codepointEnd - codepointStart;
            assert(codepointBytes <= 4);
            memcpy(codepointText, line + codepointStart, codepointBytes);
            codepointText[codepointBytes] = '\0';
            cjkWidth += renderer.getTextAdvanceX(cachedFontId, codepointText, EpdFontFamily::REGULAR);
            candidateWidth = cjkWidth;
          } else {
            cjkFastPath = false;
            char saved = '\0';
            if (codepointEnd < displayLen) {
              saved = line[codepointEnd];
              line[codepointEnd] = '\0';
            }
            candidateWidth = renderer.getTextAdvanceX(cachedFontId, line + lineBytePos, EpdFontFamily::REGULAR);
            if (codepointEnd < displayLen) {
              line[codepointEnd] = saved;
            }
          }

          if (candidateWidth > viewportWidth) {
            if (codepoint == ' ' && codepointStart > lineBytePos) {
              lastFittingSpace = codepointStart;
            }
            // A single over-wide glyph still has to be consumed as one complete
            // UTF-8 codepoint so the page index always makes progress.
            if (lastFittingPos == lineBytePos) {
              lastFittingPos = codepointEnd;
            }
            overflowed = true;
            break;
          }

          lastFittingPos = codepointEnd;
          if (codepoint == ' ' && codepointStart > lineBytePos) {
            lastFittingSpace = codepointStart;
          }
          scanPos = codepointEnd;
        }

        const size_t breakPos = overflowed && lastFittingSpace != std::string::npos ? lastFittingSpace : lastFittingPos;
        assert(breakPos > lineBytePos && breakPos <= displayLen);
        assert(breakPos == displayLen || (static_cast<uint8_t>(line[breakPos]) & 0xC0) != 0x80);

        outLines.emplace_back(line + lineBytePos, breakPos - lineBytePos);
        lineBytePos = breakPos;
        if (lineBytePos < displayLen && line[lineBytePos] == ' ') {
          lineBytePos++;
        }
      }

      line[displayLen] = lineTerminator;
    }

    // Determine how much of the source buffer we consumed
    if (lineBytePos >= displayLen) {
      // Fully consumed this source line. Only skip a byte when it is an actual newline.
      pos = lineEnd + (hasNewline ? 1 : 0);
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    const auto* nextCodepoint = buffer;
    utf8NextCodepoint(&nextCodepoint);
    pos = static_cast<size_t>(nextCodepoint - buffer);
    if (pos == 0) {
      pos = 1;  // Embedded NUL: consume the invalid byte.
    }
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  return !outLines.empty();
}

bool TxtReaderActivity::extendIndexToPage(const size_t targetPage) {
  while (!indexComplete && targetPage >= pageOffsets.size()) {
    const size_t offset = pageOffsets.back();
    size_t nextOffset = offset;
    if (!loadPageAtOffset(offset, currentPageLines, nextOffset)) {
      return false;
    }

    if (!advancePageIndex(nextOffset)) return false;
  }
  return targetPage < pageOffsets.size();
}

bool TxtReaderActivity::advancePageIndex(const size_t nextOffset) {
  const auto result = indexComplete ? txt_page_index::AdvanceResult::Unchanged
                                    : txt_page_index::recordNextOffset(pageOffsets, txt->getFileSize(), nextOffset);
  switch (result) {
    case txt_page_index::AdvanceResult::Unchanged:
      break;
    case txt_page_index::AdvanceResult::PageAdded:
      indexCacheDirty = true;
      break;
    case txt_page_index::AdvanceResult::Completed:
      indexComplete = true;
      indexCacheDirty = true;
      break;
  }
  updateTotalPages();
  if (indexCacheDirty && txt_page_index::shouldCheckpoint(pageOffsets.size(), indexComplete)) {
    savePageIndexCache();
  }
  return result != txt_page_index::AdvanceResult::Unchanged;
}

void TxtReaderActivity::updateTotalPages() {
  totalPages = txt_page_index::estimateTotalPages(txt->getFileSize(), pageOffsets, indexComplete);
}

int TxtReaderActivity::getProgressPercent() const {
  return txt_page_index::progressPercent(currentPageEndOffset, txt ? txt->getFileSize() : 0);
}

void TxtReaderActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (!pageBuffer) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (static_cast<size_t>(currentPage) >= pageOffsets.size()) {
    currentPage = static_cast<int>(pageOffsets.size() - 1);
  }

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset = offset;
  currentPageLines.clear();
  if (!loadPageAtOffset(offset, currentPageLines, nextOffset)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  currentPageEndOffset = nextOffset;

  advancePageIndex(nextOffset);

  renderer.clearScreen();
  renderPage();
  if (!firstPageLogged) {
    firstPageLogged = true;
    LOG_DBG("TRS", "First page displayed: open_total=%lums", millis() - openStartMs);
  }

  // Save progress
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  const auto t0 = millis();
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.c_str(), EpdFontFamily::REGULAR);

        // Apply text alignment
        switch (effectiveAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            // For plain text, justified is treated as left-aligned
            // (true justification would require word spacing adjustments)
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();
  fcm->logStats("txt-page");

  // BW rendering
  renderLines();
  renderStatusBar();
  const auto tBwRender = millis();

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  const auto tDisplay = millis();

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
  const auto tEnd = millis();
  LOG_DBG("TRS", "Page render: prewarm=%lums bw_render=%lums display=%lums aa=%lums total=%lums", tPrewarm - t0,
          tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - tDisplay, tEnd - t0);
  // scope destructor clears font cache via FontCacheManager
}

void TxtReaderActivity::renderStatusBar() const {
  std::string title;
  if (SETTINGS.statusBarSpec().showsTitle()) {
    title = txt->getTitle();
  }
  GUI.drawStatusBar(renderer, getProgressPercent(), currentPage + 1, totalPages, title, 0, 0, true, false,
                    !indexComplete);
}

void TxtReaderActivity::saveProgress() const {
  const int progressPercent = getProgressPercent();
  const bool completed = indexComplete && static_cast<size_t>(currentPage + 1) == pageOffsets.size();
  READING_STATS.updateProgress(static_cast<uint8_t>(progressPercent), completed, "",
                               static_cast<uint8_t>(progressPercent));

  const uint32_t page = static_cast<uint32_t>(currentPage);
  uint8_t data[4];
  data[0] = page & 0xFF;
  data[1] = (page >> 8) & 0xFF;
  data[2] = (page >> 16) & 0xFF;
  data[3] = (page >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: page %d", currentPage);
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      const uint32_t savedPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                                 (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
      if (pageOffsets.empty()) {
        currentPage = 0;
        return;
      }

      const size_t targetPage = txt_page_index::recoveryTargetPage(savedPage, pageOffsets.size());
      if (targetPage != savedPage) {
        LOG_DBG("TRS", "Progress recovery capped: saved=%u target=%u", static_cast<unsigned>(savedPage),
                static_cast<unsigned>(targetPage));
      }
      extendIndexToPage(targetPage);
      currentPage = static_cast<int>(std::min(targetPage, pageOffsets.size() - 1));
      currentPageEndOffset = pageOffsets[currentPage];
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
    }
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (using serialization module):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID (to invalidate cache on font change)
  // - int32_t: screen margin (to invalidate cache on margin change)
  // - uint8_t: paragraph alignment (to invalidate cache on alignment change)
  // - uint8_t: index complete (v5 only; v4 indexes are always complete)
  // - uint32_t: known pages count
  // - N * uint32_t: page offsets

  std::string cachePath = txt->getCachePath() + "/index.bin";
  HalFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  uint32_t magic = 0;
  if (!readPodChecked(f, magic)) return false;
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    return false;
  }

  uint8_t version = 0;
  if (!readPodChecked(f, version)) return false;
  if (!txt_page_index::isSupportedCacheVersion(version)) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, txt_page_index::CACHE_VERSION);
    return false;
  }

  uint32_t fileSize = 0;
  if (!readPodChecked(f, fileSize)) return false;
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    return false;
  }

  int32_t cachedWidth = 0;
  if (!readPodChecked(f, cachedWidth)) return false;
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    return false;
  }

  int32_t cachedLines = 0;
  if (!readPodChecked(f, cachedLines)) return false;
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    return false;
  }

  int32_t fontId = 0;
  if (!readPodChecked(f, fontId)) return false;
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    return false;
  }

  int32_t margin = 0;
  if (!readPodChecked(f, margin)) return false;
  if (margin != cachedScreenMargin) {
    LOG_DBG("TRS", "Cache screen margin mismatch, rebuilding");
    return false;
  }

  uint8_t alignment = 0;
  if (!readPodChecked(f, alignment)) return false;
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    return false;
  }

  uint8_t complete = 1;
  if (version == txt_page_index::CACHE_VERSION && !readPodChecked(f, complete)) return false;
  if (complete > 1) return false;

  uint32_t numPages = 0;
  if (!readPodChecked(f, numPages)) return false;
  const uint64_t offsetBytes = static_cast<uint64_t>(numPages) * sizeof(uint32_t);
  if (numPages > fileSize || offsetBytes > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
      f.available() != static_cast<int>(offsetBytes)) {
    LOG_DBG("TRS", "Invalid page index size: %u pages", static_cast<unsigned>(numPages));
    return false;
  }
  if ((fileSize == 0 && (numPages != 0 || complete == 0)) || (fileSize > 0 && numPages == 0)) {
    return false;
  }

  // Read page offsets
  pageOffsets.clear();
  const size_t reserveExtra = std::min<size_t>(txt_page_index::CHECKPOINT_PAGE_COUNT, fileSize - numPages);
  pageOffsets.reserve(static_cast<size_t>(numPages) + reserveExtra);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset = 0;
    if (!readPodChecked(f, offset) || offset >= fileSize || (i == 0 && offset != 0) ||
        (i > 0 && offset <= pageOffsets.back())) {
      pageOffsets.clear();
      return false;
    }
    pageOffsets.push_back(offset);
  }

  indexComplete = complete != 0;
  indexCacheDirty = version == txt_page_index::LEGACY_CACHE_VERSION;
  updateTotalPages();
  LOG_DBG("TRS", "Loaded page index cache: %u known pages, complete=%d", static_cast<unsigned>(pageOffsets.size()),
          indexComplete);
  return true;
}

bool TxtReaderActivity::savePageIndexCache() {
  const std::string cachePath = txt->getCachePath() + "/index.bin";
  const std::string tempPath = cachePath + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("TRS", tempPath, f)) {
      LOG_ERR("TRS", "Failed to open temp page index cache");
      return false;
    }

    const uint32_t fileSize = static_cast<uint32_t>(txt->getFileSize());
    const int32_t width = viewportWidth;
    const int32_t pageLines = linesPerPage;
    const int32_t fontId = cachedFontId;
    const int32_t margin = cachedScreenMargin;
    const uint8_t complete = static_cast<uint8_t>(indexComplete);
    const uint32_t pageCount = static_cast<uint32_t>(pageOffsets.size());
    if (!writePodChecked(f, CACHE_MAGIC) || !writePodChecked(f, txt_page_index::CACHE_VERSION) ||
        !writePodChecked(f, fileSize) || !writePodChecked(f, width) || !writePodChecked(f, pageLines) ||
        !writePodChecked(f, fontId) || !writePodChecked(f, margin) || !writePodChecked(f, cachedParagraphAlignment) ||
        !writePodChecked(f, complete) || !writePodChecked(f, pageCount)) {
      LOG_ERR("TRS", "Short write saving page index header");
      return false;
    }

    for (size_t offset : pageOffsets) {
      if (!writePodChecked(f, static_cast<uint32_t>(offset))) {
        LOG_ERR("TRS", "Short write saving page index offsets");
        return false;
      }
    }
    f.flush();
  }

  Storage.remove(cachePath.c_str());
  if (!Storage.rename(tempPath.c_str(), cachePath.c_str())) {
    LOG_ERR("TRS", "Failed to replace page index cache");
    return false;
  }
  indexCacheDirty = false;
  LOG_DBG("TRS", "Saved page index cache: %u known pages, complete=%d", static_cast<unsigned>(pageOffsets.size()),
          indexComplete);
  return true;
}

ScreenshotInfo TxtReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Txt;
  if (txt) {
    const std::string t = txt->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
  }
  info.currentPage = currentPage + 1;
  info.totalPages = totalPages;
  info.progressPercent = getProgressPercent();
  return info;
}
