#include "InxRecentActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/themes/inx/InxTheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr int kGap = UiHighDpiProfile::enabled ? UiHighDpiProfile::controlGap : 8;
constexpr int kPagePadding = UiHighDpiProfile::enabled ? UiHighDpiProfile::contentPadding : 18;
constexpr int kProgressHeight = 6;

const char* titleOf(const RecentBook& book) { return book.title.empty() ? book.path.c_str() : book.title.c_str(); }

void drawMiniProgress(const GfxRenderer& renderer, const Rect rect, const uint8_t percent) {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 2);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) renderer.fillRect(rect.x + 1, rect.y + 1, fillWidth, std::max(0, rect.height - 2));
}

uint8_t progressOf(const ReadingBookStats* stats) { return stats ? stats->lastProgressPercent : 0; }

void drawSparseInk(const GfxRenderer& renderer, const Rect rect) {
  for (int y = rect.y; y < rect.y + rect.height; y += 2) {
    for (int x = rect.x; x < rect.x + rect.width; x += 2) renderer.drawPixel(x, y, true);
  }
}

void drawDottedSeparator(const GfxRenderer& renderer, const int x, const int y, const int width) {
  for (int px = x; px < x + width; px += 3) renderer.drawPixel(px, y, true);
}

Rect fitCoverRect(const Rect bounds) {
  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  return Rect{bounds.x + (bounds.width - size.width) / 2, bounds.y + (bounds.height - size.height) / 2, size.width,
              size.height};
}

void drawProgressBadge(const GfxRenderer& renderer, const Rect cover, const uint8_t progress) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", static_cast<unsigned>(progress));
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, label);
  const int textHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int width = std::max(24, textWidth + 8);
  const int height = textHeight + 4;
  const int x = cover.x + cover.width - width - 2;
  const int y = cover.y + 2;
  renderer.fillRect(x, y, width, height, true);
  renderer.drawText(SMALL_FONT_ID, x + (width - textWidth) / 2, y + 2, label, false, EpdFontFamily::BOLD);
}

void drawMetric(const GfxRenderer& renderer, const int x, const int y, const char* value, const char* label,
                const int width) {
  const std::string shownValue = renderer.truncatedText(UI_12_FONT_ID, value, width, EpdFontFamily::BOLD);
  const std::string shownLabel = renderer.truncatedText(SMALL_FONT_ID, label, width);
  renderer.drawText(UI_12_FONT_ID, x, y, shownValue.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, x, y + renderer.getLineHeight(UI_12_FONT_ID) + 4, shownLabel.c_str());
}

void drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const bool author) {
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, titleOf(book), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y, title.c_str(), true, EpdFontFamily::BOLD);
  if (author && !book.author.empty()) {
    const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x,
                      y + (UiHighDpiProfile::enabled ? renderer.getLineHeight(UI_10_FONT_ID) + 6 : 27),
                      subtitle.c_str());
  }
}
}  // namespace

void InxRecentActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  moveSelectionTo(MainTabs::contentEdgeIndex(edge, books ? static_cast<int>(books->size()) : 0));
}

bool InxRecentActivity::hasContentSelection() const {
  if (!defersContentSelection()) return true;
  const int start = visibleStart.load();
  const int index = nav.selected.load();
  return UiListActivity::hasContentSelection() && index >= start && index < start + visibleCount.load();
}

int InxRecentActivity::initialConfirmIndex() const {
  return std::clamp(visibleStart.load(), 0, std::max(0, listCount() - 1));
}

bool InxRecentActivity::handleListSwipe(const MappedInputManager::SwipeDir swipe, const int startX, const int startY) {
  using Swipe = MappedInputManager::SwipeDir;
  const bool flowSwipe = layout() == InxRecentLayout::Flow && (swipe == Swipe::Left || swipe == Swipe::Right);
  if (!flowSwipe) {
    return UiListActivity::handleListSwipe(swipe, startX, startY);
  }
  const Rect carousel = InxRecentGeometry::flowCarouselRect(contentRect());
  if (startX < carousel.x || startX >= carousel.x + carousel.width || startY < carousel.y ||
      startY >= carousel.y + carousel.height)
    return false;
  const int delta = swipe == Swipe::Left ? 1 : -1;
  LOG_DBG("INX", "Recent swipe=%d from=%d delta=%d count=%d", static_cast<int>(swipe), visibleStart.load(), delta,
          listCount());
  if (listCount() > 1) {
    activityManager.focusMainTabContent();
    nav.requestScroll(delta);
    requestUpdate();
  }
  return true;
}

InxRecentLayout InxRecentActivity::layout() const {
  const auto value = static_cast<InxRecentLayout>(SETTINGS.inxRecentLayout);
  return value < InxRecentLayout::Count ? value : InxRecentLayout::Flow;
}

Rect InxRecentActivity::contentRect() const {
  Rect content = pageContentRect();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int topInset = mainTabsAtBottom() && !hasMainTabStatusBar() ? BaseTheme::headerStatusHeight() : 0;
  content.y += topInset + metrics.verticalSpacing;
  content.height -= topInset + metrics.verticalSpacing * 2;
  if (!mainTabsAtBottom()) {
    content.height -= std::max(0, InxRecentGeometry::footerReservedHeight - metrics.buttonHintsHeight);
  }
  return content;
}

const ReadingBookStats* InxRecentActivity::statsAt(const int index) const {
  return index >= 0 && index < static_cast<int>(bookStats.size()) ? bookStats[index] : nullptr;
}

void InxRecentActivity::onBooksChanged() {
  clearContentSelection();
  books = &recentBooks;
  bookStats.fill(nullptr);
  for (size_t i = 0; i < std::min(books->size(), bookStats.size()); ++i) {
    const RecentBook& book = (*books)[i];
    bookStats[i] = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  }
  selected = 0;
  thumbnailHeight = 0;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  requestUpdate();
}

void InxRecentActivity::onExit() {
  books = nullptr;
  bookStats.fill(nullptr);
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
  thumbnailHeight = 0;
  RecentBooksActivity::onExit();
}

void InxRecentActivity::setThumbnailHeight(const int displayHeight) {
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  const int height = std::max(0, displayHeight);
#else
  const int height = InxCoverGeometry::thumbnailHeightForCropFill(displayHeight);
#endif
  if (thumbnailHeight == height) return;
  thumbnailHeight = height;
  targetCoverStates.fill(CoverCacheState::Unchecked);
  fallbackCoverStates.fill(CoverCacheState::Unchecked);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  clearCoverCaches();
#endif
}

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
void InxRecentActivity::clearCoverCaches() {
  std::for_each(targetCoverCaches.begin(), targetCoverCaches.end(), [](auto& cache) { cache = {}; });
  std::for_each(fallbackCoverCaches.begin(), fallbackCoverCaches.end(), [](auto& cache) { cache = {}; });
  cachedCoverBytes = 0;
}

InxRecentActivity::CoverCacheLoadResult InxRecentActivity::tryLoadCoverCache(HalFile& file, CoverRamCache& cache) {
  if (cache.attempted) return CoverCacheLoadResult::Stream;
  cache.attempted = true;

  const size_t fileSize = file.fileSize();
  if (fileSize == 0 || fileSize > MAX_CACHED_COVER_FILE_BYTES || cachedCoverBytes > MAX_COVER_CACHE_BYTES ||
      fileSize > MAX_COVER_CACHE_BYTES - cachedCoverBytes) {
    return CoverCacheLoadResult::Stream;
  }
  if (!memory::psramHasHeadroom(fileSize, fileSize, COVER_CACHE_MAX_ALLOC_RESERVE)) {
    return CoverCacheLoadResult::Stream;
  }

  // A cover is up to 64 KB, so it cannot live on the task stack. Allocate once
  // on first use and retain it for this Activity.
  auto bytes = memory::makePsramByteBufferNoThrow(fileSize);
  if (!bytes) return CoverCacheLoadResult::Stream;
  if (!file.seek(0) || file.read(bytes.get(), fileSize) != static_cast<int>(fileSize)) {
    LOG_ERR("INX", "Short read loading cover into PSRAM (%u bytes)", static_cast<unsigned>(fileSize));
    file.seek(0);
    return CoverCacheLoadResult::Stream;
  }

  Bitmap bitmap(bytes.get(), fileSize);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    return CoverCacheLoadResult::Invalid;
  }

  cache.bytes = std::move(bytes);
  cache.size = fileSize;
  cachedCoverBytes += fileSize;
  return CoverCacheLoadResult::Loaded;
}
#endif

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state,
                                         CoverRamCache& cache) {
#else
bool InxRecentActivity::tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state) {
#endif
  switch (state) {
    case CoverCacheState::Unchecked:
      if (!Storage.exists(path.c_str())) {
        state = CoverCacheState::Missing;
        return false;
      }
      state = CoverCacheState::Ready;
      break;
    case CoverCacheState::Ready:
      break;
    case CoverCacheState::Missing:
    case CoverCacheState::Unavailable:
      return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  const auto drawCached = [this, &bounds, &state, &cache] {
    Bitmap bitmap(cache.bytes.get(), cache.size);
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
      cachedCoverBytes -= cache.size;
      cache = {};
      state = CoverCacheState::Missing;
      return false;
    }
    return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
  };

  if (cache.bytes) return drawCached();
#endif

  HalFile file;
  if (!Storage.openFileForRead("INX", path, file)) {
    state = CoverCacheState::Missing;
    return false;
  }

#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  switch (tryLoadCoverCache(file, cache)) {
    case CoverCacheLoadResult::Loaded:
      return drawCached();
    case CoverCacheLoadResult::Invalid:
      state = CoverCacheState::Missing;
      return false;
    case CoverCacheLoadResult::Stream:
      break;
  }
#endif

  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    state = CoverCacheState::Missing;
    return false;
  }
  return renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool InxRecentActivity::drawBookCover(const int bookIndex, const Rect& bounds) {
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  if (books && bookIndex >= 0 && bookIndex < static_cast<int>(books->size()) &&
      bookIndex < static_cast<int>(RecentBooksStore::MAX_RECENT_BOOKS) && thumbnailHeight > 0) {
    const RecentBook& book = (*books)[bookIndex];
    if (!book.coverBmpPath.empty()) {
      std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbnailHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex], targetCoverCaches[bookIndex])) return true;
#else
      if (tryDrawBookCover(path, bounds, targetCoverStates[bookIndex])) return true;
#endif
      if (book.coverBmpPath.find("[HEIGHT]") != std::string::npos &&
          thumbnailHeight != InxMetrics::values.homeCoverHeight) {
        path = UITheme::getCoverThumbPath(book.coverBmpPath, InxMetrics::values.homeCoverHeight);
#if defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex], fallbackCoverCaches[bookIndex])) return true;
#else
        if (tryDrawBookCover(path, bounds, fallbackCoverStates[bookIndex])) return true;
#endif
      }
    }
  }

  const auto size = InxCoverGeometry::fit(bounds.width, bounds.height);
  const int x = bounds.x + (bounds.width - size.width) / 2;
  const int y = bounds.y + (bounds.height - size.height) / 2;
  renderer.drawRect(x, y, size.width, size.height, 2, true);
  renderer.fillRect(x, y + size.height / 3, size.width, size.height * 2 / 3);
  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, x + (size.width - iconSize) / 2, y + size.height / 6 - iconSize / 2, iconSize);
  return false;
}

bool InxRecentActivity::prepareNextMissingCover() {
  if (!books || thumbnailHeight <= 0) return false;
  const size_t count = std::min(books->size(), RecentBooksStore::MAX_RECENT_BOOKS);
  for (size_t index = 0; index < count; ++index) {
    CoverCacheState& state = targetCoverStates[index];
    switch (state) {
      case CoverCacheState::Unchecked: {
        const std::string& templatePath = (*books)[index].coverBmpPath;
        if (templatePath.empty()) {
          state = CoverCacheState::Unavailable;
          continue;
        }
        state = Storage.exists(UITheme::getCoverThumbPath(templatePath, thumbnailHeight).c_str())
                    ? CoverCacheState::Ready
                    : CoverCacheState::Missing;
        if (state == CoverCacheState::Ready) continue;
        break;
      }
      case CoverCacheState::Ready:
      case CoverCacheState::Unavailable:
        continue;
      case CoverCacheState::Missing:
        break;
    }

    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      state = BookCoverLoader::ensureThumbnail((*books)[index].path, thumbnailHeight).empty()
                  ? CoverCacheState::Unavailable
                  : CoverCacheState::Ready;
    }
    requestUpdate();
    return true;
  }
  return false;
}

void InxRecentActivity::drawFlow(UiScreen& screen, const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int count = static_cast<int>(books->size());
  const RecentBook& book = (*books)[selected];
  const Rect carousel = InxRecentGeometry::flowCarouselRect(content);
  drawSparseInk(renderer, carousel);

  const auto centerSize = InxCoverGeometry::fit(carousel.width, std::max(1, carousel.height * 94 / 100));
  const Rect center{carousel.x + (carousel.width - centerSize.width) / 2,
                    carousel.y + (carousel.height - centerSize.height) / 2, centerSize.width, centerSize.height};
  setThumbnailHeight(center.height);
  const auto sideSize = InxCoverGeometry::fit(carousel.width, std::max(1, center.height * 90 / 100));
  const int sideTop = center.y + (center.height - sideSize.height) / 2;
  const int sideGap = std::max(kGap, content.width * 4 / 100);
  // The render scope clips side covers and all following text to the content area.
  if (selected > 0) {
    drawBookCover(selected - 1, Rect{center.x - sideSize.width - sideGap, sideTop, sideSize.width, sideSize.height});
  }
  if (selected + 1 < count) {
    drawBookCover(selected + 1, Rect{center.x + center.width + sideGap, sideTop, sideSize.width, sideSize.height});
  }
  drawBookCover(selected, center);
  screen.frame().hit(freeink::ui::Rect{static_cast<int16_t>(center.x), static_cast<int16_t>(center.y),
                                       static_cast<int16_t>(center.width), static_cast<int16_t>(center.height)},
                     ACTION_ROW, static_cast<int16_t>(selected), freeink::ui::InputTouch | freeink::ui::InputLongPress);
  if (showSelection && selected == nav.selected) GUI.drawSelectionFrame(renderer, center);

  const int dividerY = carousel.y + carousel.height + 10;
  renderer.drawLine(content.x, dividerY, content.x + content.width - 1, dividerY, true);
  const int textY = dividerY + 15;
  const int textX = content.x + kPagePadding;
  drawBookText(renderer, book, textX, textY, content.width - kPagePadding * 2, true);

  const ReadingBookStats* stats = statsAt(selected);
  const uint8_t progress = progressOf(stats);
  const int progressY = textY + (UiHighDpiProfile::enabled ? renderer.getLineHeight(UI_10_FONT_ID) +
                                                                 renderer.getLineHeight(SMALL_FONT_ID) + 24
                                                           : 58);
  const int progressWidth = std::max(24, (content.width - kPagePadding * 2) / 2);
  drawMiniProgress(renderer, Rect{textX, progressY, progressWidth, kProgressHeight}, progress);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
  renderer.drawText(SMALL_FONT_ID, textX + progressWidth + 12,
                    progressY - (renderer.getLineHeight(SMALL_FONT_ID) - kProgressHeight) / 2, percent);

  const int metricsTop = progressY + (UiHighDpiProfile::enabled ? 48 : 34);
  const int metricWidth = (content.width - kPagePadding * 2 - kGap) / 2;
  const int metricHeight = std::max(1, (content.y + content.height - metricsTop) / 2);
  const std::string total = stats ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) : "0m";
  const std::string last = stats ? ReadingStatsAnalytics::formatDurationHm(stats->lastSessionMs) : "0m";
  char sessions[16];
  char chapter[8];
  snprintf(sessions, sizeof(sessions), "%u", stats ? static_cast<unsigned>(stats->sessions) : 0U);
  snprintf(chapter, sizeof(chapter), "%u%%", stats ? static_cast<unsigned>(stats->chapterProgressPercent) : 0U);
  drawMetric(renderer, textX, metricsTop, total.c_str(), tr(STR_TOTAL_TIME), metricWidth);
  drawMetric(renderer, textX + metricWidth + kGap, metricsTop, sessions, tr(STR_SESSIONS), metricWidth);
  drawMetric(renderer, textX, metricsTop + metricHeight, last.c_str(), tr(STR_LAST_SESSION), metricWidth);
  drawMetric(renderer, textX + metricWidth + kGap, metricsTop + metricHeight, chapter, tr(STR_CHAPTER_PROGRESS),
             metricWidth);
}

void InxRecentActivity::drawGrid(UiScreen& screen, const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = nav.top;
  const int cellWidth = content.width / 2;
  const int cellHeight = content.height / 2;
  for (int slot = 0; slot < 4 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 2;
    const int row = slot / 2;
    const Rect cell{content.x + column * cellWidth + kGap / 2, content.y + row * cellHeight + kGap / 2,
                    cellWidth - kGap, cellHeight - kGap};
    if (showSelection && index == nav.selected) drawSparseInk(renderer, cell);
    const Rect cover = fitCoverRect(Rect{cell.x + kGap, cell.y + kGap, cell.width - kGap * 2, cell.height - kGap * 2});
    if (slot == 0) setThumbnailHeight(cover.height);
    drawBookCover(index, cover);
    screen.frame().hit(freeink::ui::Rect{static_cast<int16_t>(cell.x), static_cast<int16_t>(cell.y),
                                         static_cast<int16_t>(cell.width), static_cast<int16_t>(cell.height)},
                       ACTION_ROW, static_cast<int16_t>(index), freeink::ui::InputTouch | freeink::ui::InputLongPress);
    if (showSelection && index == nav.selected) GUI.drawSelectionFrame(renderer, cover);
    const int barWidth = std::max(24, cover.width - 30);
    const int barX = cover.x + (cover.width - barWidth) / 2;
    const int barY = cover.y + cover.height - 18;
    renderer.fillRect(barX - 2, barY - 2, barWidth + 4, kProgressHeight + 4, false);
    drawMiniProgress(renderer, Rect{barX, barY, barWidth, kProgressHeight}, progressOf(statsAt(index)));
  }
}

void InxRecentActivity::drawList(UiScreen& screen, const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = nav.top;
  const int rowHeight = content.height / 5;
  for (int slot = 0; slot < 5 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const Rect row{content.x, content.y + slot * rowHeight, content.width, rowHeight};
    screen.frame().hit(freeink::ui::Rect{static_cast<int16_t>(row.x), static_cast<int16_t>(row.y),
                                         static_cast<int16_t>(row.width), static_cast<int16_t>(row.height)},
                       ACTION_ROW, static_cast<int16_t>(index), freeink::ui::InputTouch | freeink::ui::InputLongPress);
    if (showSelection && index == nav.selected) drawSparseInk(renderer, row);
    const Rect cover = fitCoverRect(Rect{row.x + kPagePadding, row.y + 5, 88, row.height - 10});
    if (slot == 0) setThumbnailHeight(cover.height);
    drawBookCover(index, cover);
    const int textX = cover.x + cover.width + 14;
    const int textWidth = row.x + row.width - kPagePadding - textX;
    drawBookText(renderer, (*books)[index], textX, row.y + 14, textWidth, true);
    drawMiniProgress(renderer,
                     Rect{textX, row.y + row.height - 15, std::max(24, textWidth * 80 / 100), kProgressHeight},
                     progressOf(statsAt(index)));
    if (slot + 1 < 5 && start + slot + 1 < static_cast<int>(books->size())) {
      drawDottedSeparator(renderer, row.x + kGap, row.y + row.height - 1, row.width - kGap * 2);
    }
  }
}

void InxRecentActivity::drawIcons(UiScreen& screen, const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  const int start = nav.top;
  const int cellWidth = content.width / 3;
  const int cellHeight = content.height / 3;
  for (int slot = 0; slot < 9 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 3;
    const int row = slot / 3;
    const Rect cell{content.x + column * cellWidth + 5, content.y + row * cellHeight + 5, cellWidth - 10,
                    cellHeight - 10};
    const Rect cover = fitCoverRect(Rect{cell.x + 4, cell.y + 4, cell.width - 8, cell.height - 8});
    if (slot == 0) setThumbnailHeight(cover.height);
    drawBookCover(index, cover);
    screen.frame().hit(freeink::ui::Rect{static_cast<int16_t>(cell.x), static_cast<int16_t>(cell.y),
                                         static_cast<int16_t>(cell.width), static_cast<int16_t>(cell.height)},
                       ACTION_ROW, static_cast<int16_t>(index), freeink::ui::InputTouch | freeink::ui::InputLongPress);
    drawProgressBadge(renderer, cover, progressOf(statsAt(index)));
    if (showSelection && index == nav.selected)
      GUI.drawSelectionFrame(renderer, Rect{cover.x - 2, cover.y - 2, cover.width + 4, cover.height + 4});
  }
}

void InxRecentActivity::drawCover(UiScreen& screen, const Rect& content) {
  const bool showSelection = showMainTabContentSelection();
  constexpr int progressGap = 10;
  const int progressBlockHeight = progressGap + 8;
  const int targetWidth = std::max(1, content.width * 78 / 100);
  const Rect cover = fitCoverRect(Rect{content.x + (content.width - targetWidth) / 2, content.y + 6, targetWidth,
                                       std::max(1, content.height - progressBlockHeight - 12)});
  setThumbnailHeight(cover.height);
  drawBookCover(selected, cover);
  screen.frame().hit(freeink::ui::Rect{static_cast<int16_t>(cover.x), static_cast<int16_t>(cover.y),
                                       static_cast<int16_t>(cover.width), static_cast<int16_t>(cover.height)},
                     ACTION_ROW, static_cast<int16_t>(selected), freeink::ui::InputTouch | freeink::ui::InputLongPress);
  if (showSelection && selected == nav.selected) GUI.drawSelectionFrame(renderer, cover);
  const int barWidth = std::max(24, cover.width * 80 / 100);
  drawMiniProgress(renderer,
                   Rect{cover.x + (cover.width - barWidth) / 2, cover.y + cover.height + progressGap, barWidth, 8},
                   progressOf(statsAt(selected)));
}

void InxRecentActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_MENU_RECENT_BOOKS));
}

void InxRecentActivity::buildScreen(UiScreen& screen) {
  syncPagedViewport(InxRecentGeometry::itemsPerPage(layout()));
  selected = nav.top;
  const Rect content = contentRect();

  if (content.width > 0 && content.height > 0) {
    const GfxRenderer::ClipScope clip(renderer, content.x, content.y, content.width, content.height);
    if (!books || books->empty()) {
      UITheme::drawCenteredWrappedText(renderer, content, UI_12_FONT_ID, tr(STR_NO_RECENT_BOOKS), 2);
    } else {
      switch (layout()) {
        case InxRecentLayout::Flow:
          drawFlow(screen, content);
          break;
        case InxRecentLayout::Grid:
          drawGrid(screen, content);
          break;
        case InxRecentLayout::List:
          drawList(screen, content);
          break;
        case InxRecentLayout::Icons:
          drawIcons(screen, content);
          break;
        case InxRecentLayout::Cover:
          drawCover(screen, content);
          break;
        case InxRecentLayout::Count:
          break;
      }
    }
  }
}

void InxRecentActivity::drawFooter() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, false, false);
  const auto labels = mainTabButtonLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "", tr(STR_OPEN),
                                          books && books->size() > 1, false);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (usesMainTabBar() && mappedInput.hasTouch() && !mainTabsAtBottom()) {
    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    const int footerTop = renderer.getScreenHeight() - InxRecentGeometry::footerReservedHeight;
    GUI.drawMainTabStatusBar(renderer, Rect{left, footerTop, std::max(0, width - left - right),
                                            std::max(0, renderer.getScreenHeight() - bottom - footerTop)});
  } else if (!hasMainTabStatusBar()) {
    if (mainTabsAtBottom()) {
      GUI.drawMainTabStatusBar(
          renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, BaseTheme::headerStatusHeight()});
      return;
    }
    Rect battery = InxRecentGeometry::batteryRect(safeArea);
    GUI.drawBatteryRight(renderer, battery,
                         SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  }
}

void InxRecentActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));
  visibleStart.store(nav.top);
  visibleCount.store(std::min(InxRecentGeometry::itemsPerPage(layout()), std::max(0, listCount() - nav.top)));
  LOG_DBG("INX", "Recent view=%d rows=%d layout=%d", visibleStart.load(), visibleCount.load(),
          static_cast<int>(layout()));
  prepareNextMissingCover();
}
