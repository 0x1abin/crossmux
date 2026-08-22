#include "ReadingStatsActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>

#include "AppMetricCard.h"
#include "InxItemLayout.h"
#include "ReadingStatsDetailActivity.h"
#include "ReadingStatsExtendedActivity.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/HeaderDateUtils.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
constexpr unsigned long BOOK_LONG_PRESS_MS = 1000;
constexpr int SUMMARY_CARD_HEIGHT = 76;
constexpr int SUMMARY_GAP = 10;
constexpr int DETAILS_BUTTON_HEIGHT = 58;
constexpr int LIST_HEADER_HEIGHT = 34;
constexpr int LIST_HEADER_BOTTOM_GAP = 10;
constexpr int BOOK_ROW_HEIGHT = 80;
constexpr int BOOK_ROW_GAP = 10;
constexpr int BOOKS_PER_PAGE = 3;
constexpr int INX_TEXT_GAP = 4;
constexpr int INX_CELL_VERTICAL_PADDING = 12;
constexpr float kPi = 3.14159265358979323846f;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

std::string getBookSubtitle(const ReadingBookStats& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return book.completed ? std::string(tr(STR_DONE)) : std::string(tr(STR_IN_PROGRESS));
}

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value,
                    const bool showCheck = false) {
  AppMetricCard::Options options;
  options.showCheck = showCheck;
  AppMetricCard::draw(renderer, rect, label, value, options);
}

void drawMoreDetailsButton(const GfxRenderer& renderer, const Rect& rect, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  }
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const char* label = tr(STR_MORE_DETAILS);
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
  const int textX = rect.x + (rect.width - textWidth) / 2;
  const int textY = rect.y + (rect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2 + 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, label, true, EpdFontFamily::BOLD);
}

void drawMiniProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint8_t percent) {
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  const int innerWidth = std::max(0, rect.width - 4);
  const int fillWidth = innerWidth * std::min<int>(percent, 100) / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, std::max(0, rect.height - 4));
  }
}

void drawBookRow(const GfxRenderer& renderer, const Rect& rect, const ReadingBookStats& book, const bool selected) {
  if (selected) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
  } else {
    renderer.drawLine(rect.x, rect.y + rect.height, rect.x + rect.width, rect.y + rect.height);
  }

  const int sidePadding = 12;
  const int topPadding = 9;
  const int metaWidth = 88;
  const int innerX = rect.x + sidePadding;
  const int innerY = rect.y + topPadding;
  const int textWidth = rect.width - sidePadding * 2 - metaWidth;
  const int titleY = innerY;
  const int subtitleY = innerY + 26;
  const int progressBarY = rect.y + rect.height - 14;

  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, getBookTitle(book).c_str(), textWidth - 4, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, innerX, titleY, title.c_str(), true, EpdFontFamily::BOLD);

  const std::string subtitle =
      renderer.truncatedText(UI_10_FONT_ID, getBookSubtitle(book).c_str(), textWidth - 4, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, innerX, subtitleY, subtitle.c_str());

  const std::string progressText = std::to_string(book.lastProgressPercent) + "%";
  const std::string totalTimeText = ReadingStatsAnalytics::formatDurationHm(book.totalReadingMs);
  const int progressWidth = renderer.getTextWidth(UI_12_FONT_ID, progressText.c_str(), EpdFontFamily::BOLD);
  const int timeWidth = renderer.getTextWidth(UI_10_FONT_ID, totalTimeText.c_str());
  const int progressX = rect.x + rect.width - sidePadding - progressWidth;
  const int timeX = rect.x + rect.width - sidePadding - timeWidth;

  renderer.drawText(UI_12_FONT_ID, progressX, titleY, progressText.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, timeX, subtitleY, totalTimeText.c_str());

  drawMiniProgressBar(renderer, Rect{innerX, progressBarY, rect.width - sidePadding * 2, 9}, book.lastProgressPercent);
}

const char* titleOf(const ReadingBookStats& book) {
  return book.title.empty() ? book.path.c_str() : book.title.c_str();
}

void formatDuration(const uint64_t durationMs, char* output, const size_t outputSize) {
  const uint64_t minutes = durationMs / 60000ULL;
  if (minutes >= 60) {
    snprintf(output, outputSize, "%lluh %llum", static_cast<unsigned long long>(minutes / 60),
             static_cast<unsigned long long>(minutes % 60));
  } else {
    snprintf(output, outputSize, "%llum", static_cast<unsigned long long>(minutes));
  }
}

void drawClippedText(const GfxRenderer& renderer, const int fontId, const Rect rect, const char* text,
                     const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (!text || rect.width <= 0 || rect.height <= 0) return;
  GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  renderer.drawText(fontId, rect.x, rect.y, text, true, style);
}

void drawCenteredClippedText(const GfxRenderer& renderer, const int fontId, const Rect rect, const char* text,
                             const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (!text || rect.width <= 0 || rect.height <= 0) return;
  const int textWidth = renderer.getTextWidth(fontId, text, style);
  GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  renderer.drawText(fontId, rect.x + std::max(0, (rect.width - textWidth) / 2), rect.y, text, true, style);
}

void drawDottedLine(const GfxRenderer& renderer, const int x1, const int y1, const int x2, const int y2) {
  if (x1 == x2) {
    for (int y = y1; y <= y2; y += 3) renderer.drawPixel(x1, y);
  } else {
    for (int x = x1; x <= x2; x += 3) renderer.drawPixel(x, y1);
  }
}

int statsCellHeight(const GfxRenderer& renderer) {
  return renderer.getLineHeight(UI_12_FONT_ID) + INX_TEXT_GAP + renderer.getLineHeight(SMALL_FONT_ID) +
         INX_CELL_VERTICAL_PADDING;
}

void drawStatsGrid(const GfxRenderer& renderer, const Rect rect, const int rows, const char* const* values,
                   const char* const* labels) {
  if (rows <= 0 || rect.width <= 0 || rect.height <= 0) return;
  const int cellWidth = rect.width / 2;
  const int cellHeight = rect.height / rows;
  const int valueHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int textHeight = valueHeight + INX_TEXT_GAP + labelHeight;
  drawDottedLine(renderer, rect.x + cellWidth, rect.y, rect.x + cellWidth, rect.y + rect.height - 1);
  for (int row = 1; row < rows; ++row) {
    drawDottedLine(renderer, rect.x, rect.y + row * cellHeight, rect.x + rect.width - 1, rect.y + row * cellHeight);
  }

  for (int index = 0; index < rows * 2; ++index) {
    const int column = index % 2;
    const int row = index / 2;
    const int cellTop = rect.y + row * cellHeight;
    const int textY = cellTop + std::max(0, (cellHeight - textHeight) / 2);
    const Rect valueRect{rect.x + column * cellWidth + 10, textY, cellWidth - 20, valueHeight};
    drawClippedText(renderer, UI_12_FONT_ID, valueRect, values[index], EpdFontFamily::BOLD);
    drawClippedText(renderer, SMALL_FONT_ID,
                    Rect{valueRect.x, valueRect.y + valueHeight + INX_TEXT_GAP, valueRect.width, labelHeight},
                    labels[index]);
  }
}

void drawDonut(const GfxRenderer& renderer, const int centerX, const int centerY, const int radius,
               const uint8_t rawPercent) {
  const int percent = std::min<int>(rawPercent, 100);
  const int thickness = std::max(5, radius / 7);
  const int innerRadius = radius - thickness;
  for (int degree = 0; degree < 360; degree += 8) {
    const float angle = (static_cast<float>(degree) - 90.0f) * kPi / 180.0f;
    renderer.drawPixel(centerX + static_cast<int>(std::cos(angle) * (radius - thickness / 2)),
                       centerY + static_cast<int>(std::sin(angle) * (radius - thickness / 2)));
  }
  const int progressDegrees = percent * 360 / 100;
  if (percent == 100) {
    renderer.drawArc(radius, centerX, centerY, -1, -1, thickness, true);
    renderer.drawArc(radius, centerX, centerY, 1, -1, thickness, true);
    renderer.drawArc(radius, centerX, centerY, -1, 1, thickness, true);
    renderer.drawArc(radius, centerX, centerY, 1, 1, thickness, true);
  } else {
    for (int degree = 0; degree < progressDegrees; ++degree) {
      const float angle = (static_cast<float>(degree) - 90.0f) * kPi / 180.0f;
      renderer.drawLine(centerX + static_cast<int>(std::cos(angle) * innerRadius),
                        centerY + static_cast<int>(std::sin(angle) * innerRadius),
                        centerX + static_cast<int>(std::cos(angle) * radius),
                        centerY + static_cast<int>(std::sin(angle) * radius), 2, true);
    }
  }

  char label[8];
  snprintf(label, sizeof(label), "%d%%", percent);
  const int textWidth = renderer.getTextWidth(NOTOSERIF_18_FONT_ID, label, EpdFontFamily::BOLD);
  const int textHeight = renderer.getLineHeight(NOTOSERIF_18_FONT_ID);
  renderer.drawText(NOTOSERIF_18_FONT_ID, centerX - textWidth / 2, centerY - textHeight / 2, label, true,
                    EpdFontFamily::BOLD);
}

bool resolveCoverPath(const std::string& source, const int height, char* output, const size_t outputSize) {
  if (source.empty() || outputSize == 0) return false;
  constexpr const char token[] = "[HEIGHT]";
  const size_t tokenAt = source.find(token);
  if (tokenAt == std::string::npos) {
    if (source.size() >= outputSize) return false;
    memcpy(output, source.c_str(), source.size() + 1);
    return true;
  }

  char heightText[12];
  const int heightLength = snprintf(heightText, sizeof(heightText), "%d", height);
  const size_t suffixAt = tokenAt + sizeof(token) - 1;
  const size_t suffixLength = source.size() - suffixAt;
  if (heightLength <= 0 || tokenAt + static_cast<size_t>(heightLength) + suffixLength >= outputSize) return false;
  memcpy(output, source.data(), tokenAt);
  memcpy(output + tokenAt, heightText, static_cast<size_t>(heightLength));
  memcpy(output + tokenAt + heightLength, source.data() + suffixAt, suffixLength);
  output[tokenAt + heightLength + suffixLength] = '\0';
  return true;
}

bool drawCover(const GfxRenderer& renderer, const ReadingBookStats& book, const Rect bounds) {
  renderer.drawRect(bounds.x, bounds.y, bounds.width, bounds.height, 2, true);
  const auto tryDraw = [&renderer, &book, &bounds](const int height) {
    char path[256];
    if (!resolveCoverPath(book.coverBmpPath, height, path, sizeof(path))) return false;
    HalFile file;
    if (Storage.openFileForRead("INX_STATS", path, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        if (renderer.drawBitmapCropToFill(bitmap, bounds.x + 3, bounds.y + 3, bounds.width - 6, bounds.height - 6))
          return true;
      }
    }
    return false;
  };

  if (book.coverBmpPath.find("[HEIGHT]") == std::string::npos) {
    if (tryDraw(bounds.height)) return true;
  } else {
    const std::array candidateHeights = {bounds.height, 140, 160, 226, 240, 400};
    if (std::any_of(candidateHeights.begin(), candidateHeights.end(), tryDraw)) return true;
  }

  constexpr int iconSize = 32;
  renderer.drawIcon(CoverIcon, bounds.x + (bounds.width - iconSize) / 2, bounds.y + (bounds.height - iconSize) / 2,
                    iconSize);
  return false;
}
}  // namespace

void ReadingStatsActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selectedIndex = MainTabs::contentEdgeIndex(edge, static_cast<int>(READING_STATS.getBooks().size()) + 1);
}

void ReadingStatsActivity::onEnter() {
  Activity::onEnter();
  if (!usesInxLayout()) renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
  selectedIndex = usesInxLayout() ? 0 : (READING_STATS.getBooks().empty() ? 0 : 1);
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForBackRelease = false;
  waitingForCoverRender = false;
  renderedCoverMissing = false;
  renderedCoverView = -1;
  attemptedCoverView = -1;
  requestUpdate();
}

void ReadingStatsActivity::onExit() {
  // No refresh override here: returning to the Reading Stats menu stays on FAST_REFRESH (no flash).
  waitingForCoverRender = false;
  renderedCoverMissing = false;
  renderedCoverView = -1;
  attemptedCoverView = -1;
  Activity::onExit();
}

void ReadingStatsActivity::loop() {
  const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
  const int selectableCount = bookCount + 1;
  const int pageItems = BOOKS_PER_PAGE;

  if (waitForBackRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      waitForBackRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectedIndex > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
      confirmRemoveSelectedBook();
      return;
    }

    openSelectedEntry();
    return;
  }

  if (usesInxLayout()) {
    const auto moveSelection = [this, bookCount](const int delta) {
      selectedIndex = InxStatisticsGeometry::adjacentView(selectedIndex, bookCount, delta);
      requestUpdate();
    };

    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      if (selectedIndex > 0 && mappedInput.getHeldTime() >= BOOK_LONG_PRESS_MS) {
        confirmRemoveSelectedBook();
      } else {
        openSelectedEntry();
      }
      return;
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left) {
      moveSelection(1);
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Right) {
      moveSelection(-1);
      return;
    }

    buttonNavigator.onNextRelease([moveSelection] { moveSelection(1); });
    buttonNavigator.onPreviousRelease([moveSelection] { moveSelection(-1); });
    buttonNavigator.onNextContinuous([moveSelection] { moveSelection(1); });
    buttonNavigator.onPreviousContinuous([moveSelection] { moveSelection(-1); });
    if (showMainTabContentSelection()) prepareVisibleCover();
    return;
  }

  buttonNavigator.onNextRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, selectableCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, selectableCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::nextPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, selectableCount, pageItems] {
    if (selectableCount <= 1) {
      return;
    }

    if (selectedIndex == 0) {
      selectedIndex = ((selectableCount - 2) / pageItems) * pageItems + 1;
    } else {
      const int bookIndex = selectedIndex - 1;
      selectedIndex = ButtonNavigator::previousPageIndex(bookIndex, selectableCount - 1, pageItems) + 1;
    }
    requestUpdate();
  });
}

void ReadingStatsActivity::openSelectedEntry() {
  const auto& books = READING_STATS.getBooks();
  if (selectedIndex == 0) {
    startActivityForResultWith<ReadingStatsExtendedActivity>([this](const ActivityResult&) {
      guardBackReturn();
      requestUpdate();
    });
    return;
  }
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        guardBackReturn();
        requestUpdate();
      },
      books[bookIndex].path);
}

void ReadingStatsActivity::confirmRemoveSelectedBook() {
  const auto& books = READING_STATS.getBooks();
  const int bookIndex = selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) {
    return;
  }

  const ReadingBookStats selectedBook = books[bookIndex];
  const int currentSelection = selectedIndex;
  startActivityForResultWith<ConfirmationActivity>(
      [this, selectedBook, currentSelection](const ActivityResult& result) {
        if (!result.isCancelled && READING_STATS.removeBook(selectedBook.path)) {
          const int bookCount = static_cast<int>(READING_STATS.getBooks().size());
          selectedIndex = InxStatisticsGeometry::clampView(currentSelection, bookCount);
        }

        guardBackReturn();
        requestUpdate(true);
      },
      tr(STR_DELETE_STATS_ENTRY), getBookTitle(selectedBook));
}

void ReadingStatsActivity::guardBackReturn() {
  waitForBackRelease = true;
  attemptedCoverView = -1;
}

void ReadingStatsActivity::prepareVisibleCover() {
  if (waitingForCoverRender || !renderedCoverMissing || renderedCoverView != selectedIndex ||
      attemptedCoverView == selectedIndex)
    return;

  const auto& books = READING_STATS.getBooks();
  if (books.empty()) return;
  const int bookIndex = selectedIndex == 0 ? 0 : selectedIndex - 1;
  if (bookIndex < 0 || bookIndex >= static_cast<int>(books.size())) return;

  attemptedCoverView = selectedIndex;
  waitingForCoverRender = true;
  requestUpdate();
}

bool ReadingStatsActivity::usesInxLayout() const { return UITheme::getInstance().hasMainTabs(); }

void ReadingStatsActivity::render(RenderLock&&) {
  if (waitingForCoverRender) {
    const auto& books = READING_STATS.getBooks();
    const int bookIndex = selectedIndex == 0 ? 0 : selectedIndex - 1;
    if (bookIndex >= 0 && bookIndex < static_cast<int>(books.size())) {
      const std::string bookPath = books[bookIndex].path;
      std::string title;
      std::string author;
      std::string coverPath;
      if (FsHelpers::hasEpubExtension(bookPath)) {
        GfxRenderer::FrameBufferLoan loan(renderer);
        coverPath = BookCoverLoader::ensureFullCover(bookPath, &title, &author);
      } else {
        coverPath = BookCoverLoader::ensureFullCover(bookPath, &title, &author);
      }
      if (!coverPath.empty()) READING_STATS.updateBookMetadata(bookPath, title, author, coverPath);
    }
  }

  if (usesInxLayout()) {
    renderInx();
    renderedCoverView = selectedIndex;
    waitingForCoverRender = false;
    return;
  }

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;
  const int summaryTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int detailsTop = summaryTop + SUMMARY_CARD_HEIGHT * 3 + SUMMARY_GAP * 2 + metrics.verticalSpacing;
  const uint64_t todayReadingMs = READING_STATS.getTodayReadingMs();
  const std::string dailyGoalValue = ReadingStatsAnalytics::formatDurationHm(todayReadingMs) + " / " +
                                     ReadingStatsAnalytics::formatDurationHm(getDailyReadingGoalMs());

  if (usesMainTabBar()) {
    drawPageHeader(Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));
  } else {
    HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_STATS));
  }

  drawMetricCard(renderer, Rect{sidePadding, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_STREAK),
                 std::to_string(READING_STATS.getCurrentStreakDays()));
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_MAX_STREAK), std::to_string(READING_STATS.getMaxStreakDays()));
  drawMetricCard(renderer,
                 Rect{sidePadding, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_DAILY_GOAL), dailyGoalValue, todayReadingMs >= getDailyReadingGoalMs());
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + SUMMARY_CARD_HEIGHT + SUMMARY_GAP, cardWidth,
                      SUMMARY_CARD_HEIGHT},
                 tr(STR_READING_TIME), ReadingStatsAnalytics::formatDurationHm(READING_STATS.getTotalReadingMs()));
  drawMetricCard(
      renderer, Rect{sidePadding, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2, cardWidth, SUMMARY_CARD_HEIGHT},
      tr(STR_BOOKS_FINISHED), std::to_string(READING_STATS.getBooksFinishedCount()));
  drawMetricCard(renderer,
                 Rect{sidePadding + cardWidth + SUMMARY_GAP, summaryTop + (SUMMARY_CARD_HEIGHT + SUMMARY_GAP) * 2,
                      cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_STARTED), std::to_string(READING_STATS.getBooksStartedCount()));

  drawMoreDetailsButton(renderer, Rect{sidePadding, detailsTop, pageWidth - sidePadding * 2, DETAILS_BUTTON_HEIGHT},
                        selectedIndex == 0);

  const int listHeaderTop = detailsTop + DETAILS_BUTTON_HEIGHT + metrics.verticalSpacing;
  const auto& books = READING_STATS.getBooks();
  const int totalPages = std::max(1, static_cast<int>((books.size() + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE));
  const int currentPage = books.empty() || selectedIndex == 0 ? 1 : ((selectedIndex - 1) / BOOKS_PER_PAGE) + 1;
  const std::string bookCountLabel = std::to_string(currentPage) + "/" + std::to_string(totalPages);
  const std::string startedBooksLabel =
      std::string(tr(STR_STARTED_BOOKS)) + " (" + std::to_string(READING_STATS.getBooksStartedCount()) + ")";
  GUI.drawSubHeader(renderer, Rect{0, listHeaderTop, pageWidth, LIST_HEADER_HEIGHT}, startedBooksLabel.c_str(),
                    bookCountLabel.c_str());

  const int contentTop = listHeaderTop + LIST_HEADER_HEIGHT + LIST_HEADER_BOTTOM_GAP;

  if (books.empty()) {
    renderer.drawText(UI_10_FONT_ID, sidePadding, contentTop + 20, tr(STR_NO_READING_STATS));
  } else {
    const int selectedBookIndex = std::max(0, selectedIndex - 1);
    const int pageStartIndex = (selectedBookIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    const int pageEndIndex = std::min(static_cast<int>(books.size()), pageStartIndex + BOOKS_PER_PAGE);
    for (int index = pageStartIndex; index < pageEndIndex; ++index) {
      const int rowIndex = index - pageStartIndex;
      const int rowY = contentTop + rowIndex * (BOOK_ROW_HEIGHT + BOOK_ROW_GAP);
      drawBookRow(renderer, Rect{sidePadding, rowY, pageWidth - sidePadding * 2, BOOK_ROW_HEIGHT}, books[index],
                  selectedIndex == index + 1);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ReadingStatsActivity::renderInx() {
  renderer.clearScreen();
  renderedCoverMissing = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  drawPageHeader(Rect{0, metrics.topPadding, screenWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  const int contentTop = metrics.topPadding + metrics.headerHeight;
  const int contentBottom = screenHeight - metrics.buttonHintsHeight;
  const Rect content{18, contentTop + 6, screenWidth - 36, std::max(1, contentBottom - contentTop - 12)};
  const auto& books = READING_STATS.getBooks();
  const int pageTitleHeight = renderer.getLineHeight(NOTOSERIF_14_FONT_ID);
  const int bookTitleHeight = renderer.getLineHeight(NOTOSERIF_12_FONT_ID);
  const int bodyHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int cellHeight = statsCellHeight(renderer);

  if (selectedIndex == 0 || books.empty()) {
    int top = content.y;
    if (usesMainTabBar()) {
      drawCenteredClippedText(renderer, NOTOSERIF_14_FONT_ID, Rect{content.x, top + 4, content.width, pageTitleHeight},
                              tr(STR_READING_STATS), EpdFontFamily::BOLD);
      top += pageTitleHeight + 8;
    }
    const int availableHeight = std::max(1, content.y + content.height - top);
    const int gridHeight = cellHeight * 2;
    const int footerHeight = cellHeight;
    const int flexibleHeight = std::max(2, availableHeight - gridHeight - footerHeight);
    const int recentMinimumHeight = books.empty() ? bodyHeight + 16 : bookTitleHeight + bodyHeight * 2 + 32;
    const int recentHeight = std::min(flexibleHeight - 1, std::max(flexibleHeight * 43 / 100, recentMinimumHeight));
    const int donutHeight = flexibleHeight - recentHeight;
    const Rect recent{content.x, top, content.width, recentHeight};
    const Rect grid{content.x, recent.y + recent.height, content.width, gridHeight};
    const Rect donut{content.x, grid.y + grid.height, content.width, donutHeight};
    const Rect footer{content.x, donut.y + donut.height, content.width, footerHeight};

    if (books.empty()) {
      const int emptyHeight = renderer.getLineHeight(UI_12_FONT_ID);
      drawCenteredClippedText(
          renderer, UI_12_FONT_ID,
          Rect{recent.x, recent.y + std::max(0, (recent.height - emptyHeight) / 2), recent.width, emptyHeight},
          tr(STR_NO_READING_STATS));
    } else {
      const ReadingBookStats& recentBook = books.front();
      const auto coverSize = InxCoverGeometry::fit(recent.width * 28 / 100, recent.height - 12);
      const Rect cover{recent.x + 4, recent.y + (recent.height - coverSize.height) / 2, coverSize.width,
                       coverSize.height};
      renderedCoverMissing = !drawCover(renderer, recentBook, cover);

      const int textX = cover.x + cover.width + 18;
      const int textWidth = recent.x + recent.width - textX;
      const int titleY = recent.y + 8;
      drawClippedText(renderer, NOTOSERIF_12_FONT_ID, Rect{textX, titleY, textWidth, bookTitleHeight},
                      titleOf(recentBook), EpdFontFamily::BOLD);
      if (!recentBook.author.empty()) {
        drawClippedText(renderer, UI_10_FONT_ID,
                        Rect{textX, titleY + bookTitleHeight + INX_TEXT_GAP, textWidth, bodyHeight},
                        recentBook.author.c_str());
      }
      char progress[12];
      snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(recentBook.lastProgressPercent));
      const int progressTextWidth = renderer.getTextWidth(UI_10_FONT_ID, progress, EpdFontFamily::BOLD);
      const int progressY = recent.y + recent.height - bodyHeight - 16;
      drawClippedText(renderer, UI_10_FONT_ID,
                      Rect{textX, progressY, std::max(1, textWidth - progressTextWidth - 8), bodyHeight},
                      tr(STR_BOOK_PROGRESS));
      drawClippedText(renderer, UI_10_FONT_ID,
                      Rect{textX + textWidth - progressTextWidth, progressY, progressTextWidth, bodyHeight}, progress,
                      EpdFontFamily::BOLD);
      drawMiniProgressBar(renderer, Rect{textX, progressY + bodyHeight + INX_TEXT_GAP, textWidth, 8},
                          recentBook.lastProgressPercent);
    }

    const uint32_t sessions =
        std::accumulate(books.begin(), books.end(), uint32_t{0},
                        [](const uint32_t total, const ReadingBookStats& book) { return total + book.sessions; });
    char totalTime[24];
    char averageSession[24];
    char sessionCount[16];
    char streak[16];
    formatDuration(READING_STATS.getTotalReadingMs(), totalTime, sizeof(totalTime));
    formatDuration(InxStatisticsGeometry::averageSessionMs(READING_STATS.getTotalReadingMs(), sessions), averageSession,
                   sizeof(averageSession));
    snprintf(sessionCount, sizeof(sessionCount), "%u", static_cast<unsigned>(sessions));
    snprintf(streak, sizeof(streak), "%u", static_cast<unsigned>(READING_STATS.getCurrentStreakDays()));
    const char* values[] = {totalTime, averageSession, sessionCount, streak};
    const char* labels[] = {tr(STR_TOTAL_TIME), tr(STR_AVG_SESSION), tr(STR_SESSIONS), tr(STR_READ_STREAK)};
    drawStatsGrid(renderer, grid, 2, values, labels);

    const int started = static_cast<int>(READING_STATS.getBooksStartedCount());
    const int finished = static_cast<int>(READING_STATS.getBooksFinishedCount());
    const int completedPercent = started == 0 ? 0 : std::min(100, finished * 100 / started);
    const int donutRadius = std::min(std::max(18, std::min(donut.width, donut.height) * 34 / 100),
                                     std::max(18, std::min(donut.width, donut.height) / 2 - 4));
    drawDonut(renderer, donut.x + donut.width / 2, donut.y + donut.height / 2, donutRadius,
              static_cast<uint8_t>(completedPercent));

    char finishedText[16];
    char startedText[16];
    snprintf(finishedText, sizeof(finishedText), "%d", finished);
    snprintf(startedText, sizeof(startedText), "%d", started);
    const char* footerValues[] = {finishedText, startedText};
    const char* footerLabels[] = {tr(STR_BOOKS_FINISHED), tr(STR_BOOKS_STARTED)};
    drawStatsGrid(renderer, footer, 1, footerValues, footerLabels);
  } else {
    selectedIndex = InxStatisticsGeometry::clampView(selectedIndex, static_cast<int>(books.size()));
    const ReadingBookStats& book = books[selectedIndex - 1];
    const int footerHeight = bodyHeight + 8;
    const int screenTitleHeight = usesMainTabBar() ? pageTitleHeight + 8 : 0;
    const int metadataHeight = bookTitleHeight + (book.author.empty() ? 0 : bodyHeight + INX_TEXT_GAP) + 8;
    const int gridHeight = cellHeight * 3;
    const int heroHeight = std::max(1, content.height - screenTitleHeight - metadataHeight - gridHeight - footerHeight);
    const Rect screenTitle{content.x, content.y, content.width, screenTitleHeight};
    const Rect hero{content.x, screenTitle.y + screenTitle.height, content.width, heroHeight};
    const Rect metadata{content.x, hero.y + hero.height, content.width, metadataHeight};
    const Rect grid{content.x, metadata.y + metadata.height, content.width, gridHeight};
    const Rect footer{content.x, grid.y + grid.height, content.width, footerHeight};

    if (usesMainTabBar()) {
      drawClippedText(renderer, NOTOSERIF_14_FONT_ID,
                      Rect{screenTitle.x, screenTitle.y + 4, screenTitle.width, pageTitleHeight}, tr(STR_READING_STATS),
                      EpdFontFamily::BOLD);
    }

    const auto coverSize = InxCoverGeometry::fit(hero.width * 34 / 100, hero.height - 20);
    renderedCoverMissing =
        !drawCover(renderer, book,
                   Rect{hero.x + hero.width / 4 - coverSize.width / 2, hero.y + (hero.height - coverSize.height) / 2,
                        coverSize.width, coverSize.height});
    const int donutBounds = std::min(hero.width / 2, hero.height);
    const int donutRadius = std::min(std::max(18, donutBounds * 31 / 100), std::max(18, donutBounds / 2 - 4));
    drawDonut(renderer, hero.x + hero.width * 3 / 4, hero.y + hero.height / 2, donutRadius, book.lastProgressPercent);

    drawClippedText(renderer, NOTOSERIF_12_FONT_ID, Rect{metadata.x, metadata.y + 4, metadata.width, bookTitleHeight},
                    titleOf(book), EpdFontFamily::ITALIC);
    if (!book.author.empty()) {
      drawClippedText(renderer, UI_10_FONT_ID,
                      Rect{metadata.x, metadata.y + 4 + bookTitleHeight + INX_TEXT_GAP, metadata.width, bodyHeight},
                      book.author.c_str());
    }

    char totalTime[24];
    char averageSession[24];
    char sessionCount[16];
    char readingDays[16];
    char lastSession[24];
    char chapterProgress[12];
    formatDuration(book.totalReadingMs, totalTime, sizeof(totalTime));
    formatDuration(InxStatisticsGeometry::averageSessionMs(book.totalReadingMs, book.sessions), averageSession,
                   sizeof(averageSession));
    snprintf(sessionCount, sizeof(sessionCount), "%u", static_cast<unsigned>(book.sessions));
    snprintf(readingDays, sizeof(readingDays), "%u", static_cast<unsigned>(book.readingDays.size()));
    formatDuration(book.lastSessionMs, lastSession, sizeof(lastSession));
    snprintf(chapterProgress, sizeof(chapterProgress), "%u%%", static_cast<unsigned>(book.chapterProgressPercent));
    const char* values[] = {totalTime, averageSession, sessionCount, readingDays, lastSession, chapterProgress};
    const char* labels[] = {tr(STR_TOTAL_TIME), tr(STR_AVG_SESSION),  tr(STR_SESSIONS),
                            tr(STR_DAYS_READ),  tr(STR_LAST_SESSION), tr(STR_CHAPTER_PROGRESS)};
    drawStatsGrid(renderer, grid, 3, values, labels);

    char position[24];
    snprintf(position, sizeof(position), "%d/%u", selectedIndex, static_cast<unsigned>(books.size()));
    drawCenteredClippedText(renderer, UI_10_FONT_ID, Rect{footer.x, footer.y + 4, footer.width, bodyHeight}, position,
                            EpdFontFamily::BOLD);
  }

  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_SELECT), !books.empty());
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
