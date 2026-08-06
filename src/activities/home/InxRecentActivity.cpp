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
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "components/themes/inx/InxTheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr int kGap = 8;
constexpr int kPagePadding = 18;
constexpr int kProgressHeight = 6;
constexpr int kHomeBatteryWidth = 15;
constexpr int kHomeBatteryHeight = 12;

Rect contentRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  return Rect{0, top, renderer.getScreenWidth(),
              renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing};
}

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

void drawThickFrame(const GfxRenderer& renderer, const Rect rect, const int thickness = 3) {
  for (int inset = 0; inset < thickness; ++inset) {
    renderer.drawRect(rect.x - inset, rect.y - inset, rect.width + inset * 2, rect.height + inset * 2, true);
  }
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

bool tryDrawBookCover(const GfxRenderer& renderer, const std::string& path, const Rect bounds) {
  HalFile file;
  if (!Storage.openFileForRead("INX", path, file)) return false;
  Bitmap bitmap(file);
  return bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 &&
         renderer.drawBitmapCropToFill(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool drawBookCover(const GfxRenderer& renderer, const RecentBook& book, const Rect bounds, const int thumbnailHeight) {
  renderer.fillRect(bounds.x, bounds.y, bounds.width, bounds.height, false);
  if (!book.coverBmpPath.empty() && thumbnailHeight > 0) {
    std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, thumbnailHeight);
    if (tryDrawBookCover(renderer, path, bounds)) return true;
    if (book.coverBmpPath.find("[HEIGHT]") != std::string::npos &&
        thumbnailHeight != InxMetrics::values.homeCoverHeight) {
      path = UITheme::getCoverThumbPath(book.coverBmpPath, InxMetrics::values.homeCoverHeight);
      if (tryDrawBookCover(renderer, path, bounds)) return true;
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

void drawBookText(const GfxRenderer& renderer, const RecentBook& book, const int x, const int y, const int width,
                  const bool author) {
  const std::string title = renderer.truncatedText(UI_10_FONT_ID, titleOf(book), width, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x, y, title.c_str(), true, EpdFontFamily::BOLD);
  if (author && !book.author.empty()) {
    const std::string subtitle = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), width);
    renderer.drawText(SMALL_FONT_ID, x, y + 27, subtitle.c_str());
  }
}
}  // namespace

InxRecentLayout InxRecentActivity::layout() const {
  const auto value = static_cast<InxRecentLayout>(SETTINGS.inxRecentLayout);
  return value < InxRecentLayout::Count ? value : InxRecentLayout::Flow;
}

const ReadingBookStats* InxRecentActivity::statsAt(const int index) const {
  return index >= 0 && index < static_cast<int>(bookStats.size()) ? bookStats[index] : nullptr;
}

void InxRecentActivity::onEnter() {
  Activity::onEnter();
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  books = &RECENT_BOOKS.getBooks();
  bookStats.fill(nullptr);
  for (size_t i = 0; i < std::min(books->size(), bookStats.size()); ++i) {
    const RecentBook& book = (*books)[i];
    bookStats[i] = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
  }
  selected = 0;
  sawBackPress = false;
  firstRenderDone = false;
  waitingForCoverRender = false;
  nextCoverIndex = 0;
  thumbnailHeight = 0;
  requestUpdate();
}

void InxRecentActivity::onExit() {
  books = nullptr;
  bookStats.fill(nullptr);
  firstRenderDone = false;
  waitingForCoverRender = false;
  nextCoverIndex = 0;
  thumbnailHeight = 0;
  Activity::onExit();
}

void InxRecentActivity::openSelected() {
  if (!books || selected < 0 || selected >= static_cast<int>(books->size())) return;
  onSelectBook((*books)[selected].path);
}

void InxRecentActivity::prepareNextCover() {
  if (!firstRenderDone || waitingForCoverRender || !books || thumbnailHeight <= 0) return;
  const size_t count = std::min(books->size(), kMaxRecentBooks);
  if (nextCoverIndex >= count) return;

  const RecentBook& book = (*books)[nextCoverIndex++];
  bool generated = false;
  BookCoverLoader::ensureThumbnail(book.path, thumbnailHeight, &generated);
  if (generated) {
    waitingForCoverRender = true;
    requestUpdate();
  }
}

int InxRecentActivity::indexFromPoint(const int x, const int y) const {
  if (!books || books->empty()) return -1;
  const Rect content = contentRect(renderer);
  if (x < content.x || x >= content.x + content.width || y < content.y || y >= content.y + content.height) return -1;

  const InxRecentLayout currentLayout = layout();
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), currentLayout);
  int columns = 1;
  int rows = 1;
  switch (currentLayout) {
    case InxRecentLayout::Grid:
      columns = 2;
      rows = 2;
      break;
    case InxRecentLayout::List:
      rows = 5;
      break;
    case InxRecentLayout::Icons:
      columns = 3;
      rows = 3;
      break;
    case InxRecentLayout::Flow:
    case InxRecentLayout::Cover:
      return selected;
    case InxRecentLayout::Count:
      return -1;
  }

  const int column = std::min(columns - 1, (x - content.x) * columns / std::max(1, content.width));
  const int row = std::min(rows - 1, (y - content.y) * rows / std::max(1, content.height));
  const int index = start + row * columns + column;
  return index < static_cast<int>(books->size()) ? index : -1;
}

void InxRecentActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) sawBackPress = true;
  if (sawBackPress && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    sawBackPress = false;
    if (SETTINGS.standbyShortcutEnabled) activityManager.goToStandby();
    return;
  }

  if (!books || books->empty()) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0 && touched != selected) {
      selected = touched;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = indexFromPoint(x, y);
    if (touched >= 0) {
      selected = touched;
      openSelected();
    }
    return;
  }

  const int count = static_cast<int>(books->size());
  const auto swipe = mappedInput.wasSwipe();
  if (mappedInput.wasReleased(MappedInputManager::Button::Down) || swipe == MappedInputManager::SwipeDir::Up ||
      swipe == MappedInputManager::SwipeDir::Left) {
    selected = (selected + 1) % count;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) || swipe == MappedInputManager::SwipeDir::Down ||
      swipe == MappedInputManager::SwipeDir::Right) {
    selected = (selected + count - 1) % count;
    requestUpdate();
    return;
  }

  prepareNextCover();
}

void InxRecentActivity::drawFlow(const Rect& content) {
  const int count = static_cast<int>(books->size());
  const RecentBook& book = (*books)[selected];
  const int carouselHeight = std::max(1, content.height * 50 / 100);
  const Rect carousel{content.x, content.y + 5, content.width, carouselHeight};
  drawSparseInk(renderer, carousel);

  const auto centerSize = InxCoverGeometry::fit(carousel.width, std::max(1, carousel.height * 94 / 100));
  const Rect center{carousel.x + (carousel.width - centerSize.width) / 2,
                    carousel.y + (carousel.height - centerSize.height) / 2, centerSize.width, centerSize.height};
  thumbnailHeight = InxCoverGeometry::thumbnailHeightForCropFill(center.height);
  const auto sideSize = InxCoverGeometry::fit(carousel.width, std::max(1, center.height * 90 / 100));
  const int sideTop = center.y + (center.height - sideSize.height) / 2;
  const int sideGap = std::max(kGap, content.width * 4 / 100);
  if (selected > 0) {
    drawBookCover(renderer, (*books)[selected - 1],
                  Rect{center.x - sideSize.width - sideGap, sideTop, sideSize.width, sideSize.height}, thumbnailHeight);
  }
  if (selected + 1 < count) {
    drawBookCover(renderer, (*books)[selected + 1],
                  Rect{center.x + center.width + sideGap, sideTop, sideSize.width, sideSize.height}, thumbnailHeight);
  }
  drawBookCover(renderer, book, center, thumbnailHeight);
  drawThickFrame(renderer, center);

  const int dividerY = carousel.y + carousel.height + 10;
  renderer.drawLine(content.x, dividerY, content.x + content.width - 1, dividerY, true);
  const int textY = dividerY + 15;
  drawBookText(renderer, book, kPagePadding, textY, content.width - kPagePadding * 2, true);

  const ReadingBookStats* stats = statsAt(selected);
  const uint8_t progress = progressOf(stats);
  const int progressY = textY + 58;
  const int progressWidth = std::max(24, (content.width - kPagePadding * 2) / 2);
  drawMiniProgress(renderer, Rect{kPagePadding, progressY, progressWidth, kProgressHeight}, progress);
  char percent[8];
  snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(progress));
  renderer.drawText(SMALL_FONT_ID, kPagePadding + progressWidth + 12,
                    progressY - (renderer.getLineHeight(SMALL_FONT_ID) - kProgressHeight) / 2, percent);

  const int metricsTop = progressY + 34;
  const int metricWidth = (content.width - kPagePadding * 2 - kGap) / 2;
  const int metricHeight = std::max(1, (content.y + content.height - metricsTop) / 2);
  const std::string total = stats ? ReadingStatsAnalytics::formatDurationHm(stats->totalReadingMs) : "0m";
  const std::string last = stats ? ReadingStatsAnalytics::formatDurationHm(stats->lastSessionMs) : "0m";
  char sessions[16];
  char chapter[8];
  snprintf(sessions, sizeof(sessions), "%u", stats ? static_cast<unsigned>(stats->sessions) : 0U);
  snprintf(chapter, sizeof(chapter), "%u%%", stats ? static_cast<unsigned>(stats->chapterProgressPercent) : 0U);
  drawMetric(renderer, kPagePadding, metricsTop, total.c_str(), tr(STR_TOTAL_TIME), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop, sessions, tr(STR_SESSIONS), metricWidth);
  drawMetric(renderer, kPagePadding, metricsTop + metricHeight, last.c_str(), tr(STR_LAST_SESSION), metricWidth);
  drawMetric(renderer, kPagePadding + metricWidth + kGap, metricsTop + metricHeight, chapter, tr(STR_CHAPTER_PROGRESS),
             metricWidth);
}

void InxRecentActivity::drawGrid(const Rect& content) {
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 2;
  const int cellHeight = content.height / 2;
  for (int slot = 0; slot < 4 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 2;
    const int row = slot / 2;
    const Rect cell{content.x + column * cellWidth + kGap / 2, content.y + row * cellHeight + kGap / 2,
                    cellWidth - kGap, cellHeight - kGap};
    if (index == selected) drawSparseInk(renderer, cell);
    const Rect cover = fitCoverRect(Rect{cell.x + kGap, cell.y + kGap, cell.width - kGap * 2, cell.height - kGap * 2});
    if (slot == 0) thumbnailHeight = InxCoverGeometry::thumbnailHeightForCropFill(cover.height);
    drawBookCover(renderer, (*books)[index], cover, thumbnailHeight);
    if (index == selected) drawThickFrame(renderer, cover);
    const int barWidth = std::max(24, cover.width - 30);
    const int barX = cover.x + (cover.width - barWidth) / 2;
    const int barY = cover.y + cover.height - 18;
    renderer.fillRect(barX - 2, barY - 2, barWidth + 4, kProgressHeight + 4, false);
    drawMiniProgress(renderer, Rect{barX, barY, barWidth, kProgressHeight}, progressOf(statsAt(index)));
  }
}

void InxRecentActivity::drawList(const Rect& content) {
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int rowHeight = content.height / 5;
  for (int slot = 0; slot < 5 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const Rect row{content.x, content.y + slot * rowHeight, content.width, rowHeight};
    if (index == selected) drawSparseInk(renderer, row);
    const Rect cover = fitCoverRect(Rect{row.x + kPagePadding, row.y + 5, 88, row.height - 10});
    if (slot == 0) thumbnailHeight = InxCoverGeometry::thumbnailHeightForCropFill(cover.height);
    drawBookCover(renderer, (*books)[index], cover, thumbnailHeight);
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

void InxRecentActivity::drawIcons(const Rect& content) {
  const int start = InxRecentGeometry::pageStart(selected, static_cast<int>(books->size()), layout());
  const int cellWidth = content.width / 3;
  const int cellHeight = content.height / 3;
  for (int slot = 0; slot < 9 && start + slot < static_cast<int>(books->size()); ++slot) {
    const int index = start + slot;
    const int column = slot % 3;
    const int row = slot / 3;
    const Rect cell{content.x + column * cellWidth + 5, content.y + row * cellHeight + 5, cellWidth - 10,
                    cellHeight - 10};
    const Rect cover = fitCoverRect(Rect{cell.x + 4, cell.y + 4, cell.width - 8, cell.height - 8});
    if (slot == 0) thumbnailHeight = InxCoverGeometry::thumbnailHeightForCropFill(cover.height);
    drawBookCover(renderer, (*books)[index], cover, thumbnailHeight);
    drawProgressBadge(renderer, cover, progressOf(statsAt(index)));
    if (index == selected) drawThickFrame(renderer, Rect{cover.x - 2, cover.y - 2, cover.width + 4, cover.height + 4});
  }
}

void InxRecentActivity::drawCover(const Rect& content) {
  const RecentBook& book = (*books)[selected];
  constexpr int progressGap = 10;
  const int progressBlockHeight = progressGap + 8;
  const int targetWidth = std::max(1, content.width * 78 / 100);
  const Rect cover = fitCoverRect(Rect{content.x + (content.width - targetWidth) / 2, content.y + 6, targetWidth,
                                       std::max(1, content.height - progressBlockHeight - 12)});
  thumbnailHeight = InxCoverGeometry::thumbnailHeightForCropFill(cover.height);
  drawBookCover(renderer, book, cover, thumbnailHeight);
  drawThickFrame(renderer, cover);
  const int barWidth = std::max(24, cover.width * 80 / 100);
  drawMiniProgress(renderer,
                   Rect{cover.x + (cover.width - barWidth) / 2, cover.y + cover.height + progressGap, barWidth, 8},
                   progressOf(statsAt(selected)));
}

void InxRecentActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  drawPageHeader(Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));
  const Rect content = contentRect(renderer);
  thumbnailHeight = 0;

  if (!books || books->empty()) {
    UITheme::drawCenteredWrappedText(renderer, content, UI_12_FONT_ID, tr(STR_NO_RECENT_BOOKS), 2);
  } else {
    switch (layout()) {
      case InxRecentLayout::Flow:
        drawFlow(content);
        break;
      case InxRecentLayout::Grid:
        drawGrid(content);
        break;
      case InxRecentLayout::List:
        drawList(content);
        break;
      case InxRecentLayout::Icons:
        drawIcons(content);
        break;
      case InxRecentLayout::Cover:
        drawCover(content);
        break;
      case InxRecentLayout::Count:
        break;
    }
  }

  const auto labels =
      mappedInput.mapLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "", tr(STR_OPEN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawBatteryRight(renderer,
                       Rect{renderer.getScreenWidth() - kPagePadding - kHomeBatteryWidth,
                            renderer.getScreenHeight() - 30, kHomeBatteryWidth, kHomeBatteryHeight},
                       SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  renderer.displayBuffer();
  firstRenderDone = true;
  waitingForCoverRender = false;
}
