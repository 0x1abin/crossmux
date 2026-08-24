#include "TxtReaderActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <TxtEncoding.h>
#include <TxtPageIndex.h>
#include <TxtParagraph.h>
#include <Utf8.h>

#include <algorithm>
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
#include "TxtReaderChapterSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AchievementPopupUtils.h"
#include "util/ReadingBackground.h"
#include "util/ReadingGuideLine.h"

namespace {
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
// Source and worst-case 1.5x UTF-8 output coexist in the existing buffer.
constexpr size_t GBK_RAW_CHUNK_SIZE = CHUNK_SIZE * 2 / 5;
constexpr uint32_t CACHE_MAGIC = 0x54585449;            // "TXTI"
constexpr uint32_t PROGRESS_OFFSET_MAGIC = 0x4F545854;  // "TXTO"

uint32_t readLittleEndianU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLittleEndianU32(uint8_t* data, const uint32_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

template <typename T>
bool readPodChecked(HalFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T)) == static_cast<int>(sizeof(T));
}

template <typename T>
bool writePodChecked(HalFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}
}  // namespace

bool TxtReaderActivity::loadBook() {
  openStartMs = millis();
  txt = makeUniqueNoThrow<Txt>(bookPath, "/.crosspoint");
  if (!txt) {
    LOG_ERR("TRS", "OOM: TXT object (%u bytes)", static_cast<unsigned>(sizeof(Txt)));
    return false;
  }
  if (!txt->load()) {
    LOG_ERR("TRS", "Failed to load TXT: %s", bookPath.c_str());
    return false;
  }
  txt->setupCacheDir();

  // Allocated once and reused; putting 8193 bytes on the render task's 8KB stack would overflow it.
  pageBuffer = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE + 1);
  if (!pageBuffer) {
    LOG_ERR("TRS", "OOM: TXT page buffer (%u bytes)", static_cast<unsigned>(CHUNK_SIZE + 1));
    return false;
  }

  const auto fileName = bookPath.substr(bookPath.rfind('/') + 1);
  READING_STATS.beginSession(bookPath, fileName, "", "", 0, "", 0);
  return true;
}

void TxtReaderActivity::onExit() {
  ReaderActivity::onExit();

  if (indexCacheDirty) {
    savePageIndexCache();
  }
  pageOffsets.clear();
  directPageCount = 0;
  currentPageLines.clear();
  pageBuffer.reset();
  READING_STATS.endSession();
  ACHIEVEMENTS.recordSessionEnded(READING_STATS.getLastSessionSnapshot());
  showPendingAchievementPopups(renderer);
  txt.reset();

#if FREEINK_DEVICE_EEGO_A4
  // A4's single-pass grayscale path needs a clean first frame after exit.
  renderer.requestNextFullRefresh();
#endif
}

bool TxtReaderActivity::handleFormatInput() {
  READING_STATS.tickActiveSession();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      ReaderUtils::isTouchMenuGesture(renderer, mappedInput)) {
    READING_STATS.noteActivity();
    openChapterSelection();
    return true;
  }
  return false;
}

bool TxtReaderActivity::pageTurn(const bool isForward) {
  if (!initialized || pageOffsets.empty()) return false;
  READING_STATS.noteActivity();
  RenderLock lock(*this);
  endOfBook = false;
  if (pageMode == PageMode::Indexed) {
    if (!isForward && currentPage > 0) {
      currentPage--;
      return true;
    }
    if (isForward && static_cast<size_t>(currentPage + 1) < pageOffsets.size()) {
      currentPage++;
      return true;
    }
    if (isForward && indexComplete) endOfBook = true;
    return endOfBook;
  }

  if (!isForward) {
    if (directPageIndex > 0) {
      directPageIndex--;
    } else {
      pageMode = PageMode::Indexed;
      currentPage = directReturnPage;
    }
    return true;
  }
  if (directPageIndex + 1 < directPageCount) {
    directPageIndex++;
    return true;
  }
  if (currentPageEndOffset >= txt->getFileSize()) {
    endOfBook = true;
    return true;
  }
  if (directPageCount < directPageOffsets.size()) {
    directPageOffsets[directPageCount++] = currentPageEndOffset;
    directPageIndex++;
  } else {
    std::move(directPageOffsets.begin() + 1, directPageOffsets.end(), directPageOffsets.begin());
    directPageOffsets.back() = currentPageEndOffset;
  }
  return true;
}

bool TxtReaderActivity::skipPages(const int amount) {
  if (!initialized || pageOffsets.empty() || amount == 0) return false;
  RenderLock lock(*this);
  endOfBook = false;
  if (pageMode == PageMode::Direct) {
    pageMode = PageMode::Indexed;
    currentPage = directReturnPage;
  }
  if (amount < 0) {
    const int target = std::max(0, currentPage + amount);
    if (target == currentPage) return false;
    currentPage = target;
    return true;
  }

  const size_t target = static_cast<size_t>(currentPage) + static_cast<size_t>(amount);
  extendIndexToPage(target);
  if (target < pageOffsets.size()) {
    currentPage = static_cast<int>(target);
    return true;
  }
  if (indexComplete) {
    currentPage = static_cast<int>(pageOffsets.size() - 1);
    endOfBook = true;
    return true;
  }
  return false;
}

void TxtReaderActivity::openChapterSelection() {
  if (!txt || !pageBuffer || pageOffsets.empty()) return;

  uint32_t chapterCount = 0;
  bool hasCachedIndex = false;
  {
    HalFile chapterFile;
    hasCachedIndex = txt->openChapterIndex(chapterFile, textEncoding, chapterCount);
  }
  if (!hasCachedIndex) {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 1;
    if (!txt->buildChapterIndex(textEncoding, pageBuffer.get(), CHUNK_SIZE + 1, chapterCount)) {
      LOG_ERR("TRS", "Failed to build TXT chapter index");
      renderer.clearScreen();
      GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
      return;
    }
  }

  const uint32_t currentOffset = static_cast<uint32_t>(getCurrentSourceOffset());
  // The selector outlives this call and therefore cannot live on the stack.
  auto selector = makeUniqueNoThrow<TxtReaderChapterSelectionActivity>(renderer, mappedInput, *txt, currentOffset);
  if (!selector) {
    LOG_ERR("TRS", "OOM: TxtReaderChapterSelectionActivity (%u bytes)",
            static_cast<unsigned>(sizeof(TxtReaderChapterSelectionActivity)));
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
    READING_STATS.resumeSession();
    const auto* selected = std::get_if<TxtOffsetResult>(&result.data);
    if (result.isCancelled || !selected) return;

    RenderLock lock(*this);
    const int returnPage = pageMode == PageMode::Indexed ? currentPage : directReturnPage;
    goToSourceOffset(selected->sourceOffset, returnPage);
  });
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
#if FREEINK_DEVICE_EEGO_A4
  // The A4's status bar is lifted 4 px so the bezel does not cover it (see
  // BaseTheme::drawStatusBar); reserve the same space for the content.
  cachedOrientedMarginBottom += 4;
#endif

  viewportWidth = renderer.getScreenWidth() - cachedOrientedMarginLeft - cachedOrientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - cachedOrientedMarginTop - cachedOrientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);
  if (SETTINGS.extraParagraphSpacing == 0) {
    const int cjkAdvance = renderer.getTextAdvanceX(cachedFontId, "我", EpdFontFamily::REGULAR);
    paragraphIndentWidth =
        cjkAdvance > 0 ? cjkAdvance * 2 : renderer.getSpaceWidth(cachedFontId, EpdFontFamily::REGULAR) * 3;
  }

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

void TxtReaderActivity::probeTextEncoding() {
#ifdef ENABLE_CHINESE_VERSION
  if (!pageBuffer || txt->getFileSize() == 0) return;
  const size_t sampleLength = std::min(CHUNK_SIZE, txt->getFileSize());
  if (!txt->readContent(pageBuffer.get(), 0, sampleLength)) return;
  textEncoding = txt_encoding::detect(pageBuffer.get(), sampleLength, sampleLength == txt->getFileSize());
  LOG_DBG("TRS", "TXT encoding probe: %u", static_cast<unsigned>(textEncoding));
#endif
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<TxtLine>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (!pageBuffer || offset >= fileSize) {
    return false;
  }

  const size_t readLimit = textEncoding == txt_encoding::Encoding::Gbk ? GBK_RAW_CHUNK_SIZE : CHUNK_SIZE;
  size_t rawChunkSize = std::min(readLimit, fileSize - offset);
  uint8_t* const buffer = pageBuffer.get();

  if (!txt->readContent(buffer, offset, rawChunkSize)) {
    return false;
  }

#ifdef ENABLE_CHINESE_VERSION
  if (textEncoding == txt_encoding::Encoding::Unknown) {
    const auto detected = txt_encoding::detect(buffer, rawChunkSize, offset + rawChunkSize == fileSize);
    if (detected != txt_encoding::Encoding::Unknown) {
      textEncoding = detected;
      indexCacheDirty = true;
    }
  }
#endif

  size_t chunkSize = rawChunkSize;
  if (textEncoding == txt_encoding::Encoding::Gbk) {
    rawChunkSize = std::min(rawChunkSize, GBK_RAW_CHUNK_SIZE);
    const auto result =
        txt_encoding::transcodeGbkInPlace(buffer, rawChunkSize, CHUNK_SIZE + 1, offset + rawChunkSize == fileSize);
    if (result.rawLength == 0 || result.utf8Length == 0) return false;
    rawChunkSize = result.rawLength;
    chunkSize = result.utf8Length;
  } else {
    buffer[chunkSize] = '\0';

    // Leave an incomplete UTF-8 sequence at a non-final chunk boundary for the
    // next read instead of measuring or indexing a partial codepoint.
    if (offset + chunkSize < fileSize) {
      chunkSize = static_cast<size_t>(
          utf8SafeTruncateBuffer(reinterpret_cast<const char*>(buffer), static_cast<int>(chunkSize)));
      rawChunkSize = chunkSize;
      buffer[chunkSize] = '\0';
    }
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
  const bool compactParagraphs = SETTINGS.extraParagraphSpacing == 0;
  const bool naturalAlignment = cachedParagraphAlignment == CrossPointSettings::LEFT_ALIGN ||
                                cachedParagraphAlignment == CrossPointSettings::JUSTIFIED;
  txt_paragraph::State paragraphState(offset == 0);

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }
    const bool hasNewline = lineEnd < chunkSize;

    // Check if we have a complete line
    const bool lineComplete = hasNewline || (offset + rawChunkSize >= fileSize && lineEnd == chunkSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    size_t lineBytePos = 0;
    bool indentFirstVisualLine = false;

    if (compactParagraphs) {
      const auto info = txt_paragraph::analyzeLine(buffer + pos, displayLen);
      if (info.kind == txt_paragraph::LineKind::Blank) {
        paragraphState.noteBlankLine();
        pos = lineEnd + (hasNewline ? 1 : 0);
        continue;
      }
      indentFirstVisualLine = paragraphState.consume(info.kind) && naturalAlignment;
      lineBytePos = info.contentOffset;
    }

    if (displayLen == 0) {
      outLines.emplace_back();
    } else {
      char* const line = reinterpret_cast<char*>(buffer + pos);
      const char lineTerminator = line[displayLen];
      line[displayLen] = '\0';

      if (indentFirstVisualLine && BidiUtils::startsWithRtl(line + lineBytePos, BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH)) {
        indentFirstVisualLine = false;
      }
      if (!indentFirstVisualLine && compactParagraphs) {
        lineBytePos = 0;
      }

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

          const int effectiveWidth = std::max(1, viewportWidth - (indentFirstVisualLine ? paragraphIndentWidth : 0));
          if (candidateWidth > effectiveWidth) {
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

        outLines.emplace_back();
        outLines.back().text.assign(line + lineBytePos, breakPos - lineBytePos);
        outLines.back().indented = indentFirstVisualLine;
        indentFirstVisualLine = false;
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

  const size_t sourceBytes =
      textEncoding == txt_encoding::Encoding::Gbk ? txt_encoding::gbkSourceLength(buffer, pos) : pos;
  nextOffset = offset + sourceBytes;

  if (outLines.empty() && pos > 0) {
    outLines.emplace_back();
  }

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

void TxtReaderActivity::goToSourceOffset(const size_t sourceOffset, const int returnPage) {
  if (pageOffsets.empty() || sourceOffset >= txt->getFileSize()) return;

  if (indexComplete || sourceOffset <= pageOffsets.back()) {
    pageMode = PageMode::Indexed;
    currentPage = static_cast<int>(txt_page_index::pageForOffset(pageOffsets, sourceOffset));
    directPageCount = 0;
  } else {
    pageMode = PageMode::Direct;
    directPageOffsets[0] = sourceOffset;
    directPageIndex = 0;
    directPageCount = 1;
    directReturnPage = std::clamp(returnPage, 0, static_cast<int>(pageOffsets.size() - 1));
  }
  currentPageEndOffset = sourceOffset;
}

size_t TxtReaderActivity::getCurrentSourceOffset() const {
  switch (pageMode) {
    case PageMode::Indexed:
      return pageOffsets[currentPage];
    case PageMode::Direct:
      return directPageOffsets[directPageIndex];
  }
  return 0;
}

int TxtReaderActivity::getDisplayPageNumber() const {
  if (pageMode == PageMode::Indexed) return currentPage + 1;
  return txt_page_index::estimatedPageNumber(txt->getFileSize(), getCurrentSourceOffset(), totalPages);
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

void TxtReaderActivity::renderBook() {
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
  if (pageMode == PageMode::Indexed) {
    if (currentPage < 0) currentPage = 0;
    if (static_cast<size_t>(currentPage) >= pageOffsets.size()) {
      currentPage = static_cast<int>(pageOffsets.size() - 1);
    }
  } else if (directPageCount == 0 || directPageIndex >= directPageCount) {
    pageMode = PageMode::Indexed;
    currentPage = std::clamp(directReturnPage, 0, static_cast<int>(pageOffsets.size() - 1));
  }

  // Load current page content
  const size_t offset = getCurrentSourceOffset();
  size_t nextOffset = offset;
  currentPageLines.clear();
  if (!loadPageAtOffset(offset, currentPageLines, nextOffset)) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  currentPageEndOffset = nextOffset;

  if (pageMode == PageMode::Indexed) advancePageIndex(nextOffset);

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
  const int contentBottom = renderer.getScreenHeight() - cachedOrientedMarginBottom;
  auto* fcm = renderer.getFontCacheManager();

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = cachedOrientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.text.empty()) {
        int x = cachedOrientedMarginLeft + (line.indented ? paragraphIndentWidth : 0);
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.text.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        uint8_t effectiveAlignment = cachedParagraphAlignment;
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(cachedFontId, line.text.c_str(), EpdFontFamily::REGULAR);

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

        renderer.drawText(cachedFontId, x, y, line.text.c_str());
        const int guideY = y + lineHeight + SETTINGS.readingGuideLineOffset;
        if (SETTINGS.readingGuideLineEnabled && !fcm->isScanning() &&
            readingGuideLine::fitsVertically(SETTINGS.readingGuideLineStyle, guideY, cachedOrientedMarginTop,
                                             contentBottom)) {
          readingGuideLine::draw(renderer, cachedOrientedMarginLeft, guideY,
                                 cachedOrientedMarginLeft + contentWidth - 1, SETTINGS.readingGuideLineStyle);
        }
      }
      y += lineHeight;
    }
  };

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto scope = fcm->createPrewarmScope();
  renderLines();  // scan pass — text accumulated, no drawing
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();
  fcm->logStats("txt-page");

  // BW rendering
  if (SETTINGS.readingBackgroundEnabled && !readingBackground::load(renderer)) renderer.clearScreen();
  renderLines();
  renderStatusBar();
  const auto tBwRender = millis();

  // Serialize SD access in this render path against the main task's SD writes
  // (progress, index cache) so they cannot interleave mid-FAT-op.
#if FREEINK_DEVICE_EEGO_A4 && !defined(SIMULATOR)
  HalStorage::StorageLock storageLock;
#endif

#if FREEINK_DEVICE_EEGO_A4
  if (SETTINGS.textAntiAliasing) {
    // A4 single-refresh grayscale path: content + status bar are already in the
    // framebuffer, so renderAntiAliased stores them, clears, re-renders the same
    // content AND status bar in gray and displays once. We skip the BW display
    // so there is no BW-then-AA double refresh (which flashed and left the bottom
    // status bar wiped), and the gray pass includes the status bar so it stays
    // visible on the final frame.
    const auto mode = ReaderUtils::consumeRefreshMode(pagesUntilFullRefresh);
    if (mode == HalDisplay::HALF_REFRESH) renderer.displayGrayscaleBase(mode);
    ReaderUtils::renderAntiAliased(renderer, [this, &renderLines]() {
      renderLines();
      renderStatusBar();
    });
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
#else
  // Other devices keep the upstream behavior: show the BW frame first, then
  // the gray pass.
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
#endif
  const auto tDisplay = millis();
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
  GUI.drawStatusBar(renderer, getProgressPercent(), getDisplayPageNumber(), totalPages, title, 0, 0, true, false,
                    !indexComplete || pageMode == PageMode::Direct);
}

void TxtReaderActivity::saveProgress() const {
  const int progressPercent = getProgressPercent();
  const bool completed = txt && currentPageEndOffset >= txt->getFileSize();
  READING_STATS.updateProgress(static_cast<uint8_t>(progressPercent), completed, "",
                               static_cast<uint8_t>(progressPercent));

  const uint32_t sourceOffset = static_cast<uint32_t>(getCurrentSourceOffset());
  uint8_t data[8];
  writeLittleEndianU32(data, PROGRESS_OFFSET_MAGIC);
  writeLittleEndianU32(data + sizeof(uint32_t), sourceOffset);
  if (!ProgressFile::writeAtomic(txt->getCachePath(), data, sizeof(data))) {
    LOG_ERR("TRS", "Failed to save progress: offset %u", static_cast<unsigned>(sourceOffset));
  }
}

void TxtReaderActivity::loadProgress() {
  HalFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    if (f.fileSize64() == 8) {
      uint8_t data[8];
      if (f.read(data, sizeof(data)) == static_cast<int>(sizeof(data)) &&
          readLittleEndianU32(data) == PROGRESS_OFFSET_MAGIC) {
        const uint32_t sourceOffset = readLittleEndianU32(data + sizeof(uint32_t));
        if (!pageOffsets.empty() && sourceOffset < txt->getFileSize()) {
          goToSourceOffset(sourceOffset, static_cast<int>(pageOffsets.size() - 1));
          LOG_DBG("TRS", "Loaded progress: offset %u", static_cast<unsigned>(sourceOffset));
        }
      }
      return;
    }

    if (f.fileSize64() != 4) return;
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      const uint32_t savedPage = readLittleEndianU32(data);
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
  // - uint8_t: extra paragraph spacing (v7+)
  // - uint8_t: index complete (v5+; v4 indexes are always complete)
  // - uint8_t: text encoding (v6+)
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

  uint8_t cachedExtraParagraphSpacing = 1;
  if (version >= txt_page_index::PARAGRAPH_LAYOUT_CACHE_VERSION) {
    if (!readPodChecked(f, cachedExtraParagraphSpacing) || cachedExtraParagraphSpacing > 1) return false;
  }
  if (!txt_page_index::canReuseParagraphLayout(version, SETTINGS.extraParagraphSpacing != 0,
                                               cachedExtraParagraphSpacing != 0)) {
    LOG_DBG("TRS", "Cache paragraph spacing mismatch, rebuilding");
    return false;
  }

  probeTextEncoding();
  if (!txt_page_index::canReuseCacheVersion(version, textEncoding == txt_encoding::Encoding::Utf8)) {
    LOG_DBG("TRS", "Cache encoding is incompatible with version %d, rebuilding", version);
    return false;
  }

  uint8_t complete = 1;
  if (version >= txt_page_index::LAZY_CACHE_VERSION && !readPodChecked(f, complete)) return false;
  if (complete > 1) return false;

  txt_encoding::Encoding cachedEncoding = txt_encoding::Encoding::Utf8;
  if (version >= txt_page_index::ENCODING_CACHE_VERSION) {
    uint8_t serializedEncoding = 0;
    if (!readPodChecked(f, serializedEncoding) || !txt_encoding::isSerializedValueValid(serializedEncoding))
      return false;
    cachedEncoding = static_cast<txt_encoding::Encoding>(serializedEncoding);
    if (!txt_encoding::isSupported(cachedEncoding)) return false;
    if (textEncoding != txt_encoding::Encoding::Unknown && cachedEncoding != txt_encoding::Encoding::Unknown &&
        textEncoding != cachedEncoding) {
      LOG_DBG("TRS", "Cache encoding mismatch, rebuilding");
      return false;
    }
    if (textEncoding == txt_encoding::Encoding::Unknown) textEncoding = cachedEncoding;
  }

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
  indexCacheDirty = version != txt_page_index::CACHE_VERSION || cachedEncoding != textEncoding;
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
    const uint8_t encoding = static_cast<uint8_t>(textEncoding);
    const uint32_t pageCount = static_cast<uint32_t>(pageOffsets.size());
    if (!writePodChecked(f, CACHE_MAGIC) || !writePodChecked(f, txt_page_index::CACHE_VERSION) ||
        !writePodChecked(f, fileSize) || !writePodChecked(f, width) || !writePodChecked(f, pageLines) ||
        !writePodChecked(f, fontId) || !writePodChecked(f, margin) || !writePodChecked(f, cachedParagraphAlignment) ||
        !writePodChecked(f, SETTINGS.extraParagraphSpacing) || !writePodChecked(f, complete) ||
        !writePodChecked(f, encoding) || !writePodChecked(f, pageCount)) {
      LOG_ERR("TRS", "Short write saving page index header");
      return false;
    }

    // The sequential overload preserves offset order and stops at the first failed write.
    const bool shortWrite = std::any_of(pageOffsets.begin(), pageOffsets.end(), [&f](const size_t offset) {
      return !writePodChecked(f, static_cast<uint32_t>(offset));
    });
    if (shortWrite) {
      LOG_ERR("TRS", "Short write saving page index offsets");
      return false;
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
  info.currentPage = getDisplayPageNumber();
  info.totalPages = totalPages;
  info.progressPercent = getProgressPercent();
  return info;
}
