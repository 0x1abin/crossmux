#include "ReadingDayDetailActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "AppMetricCard.h"
#include "ReadingStatsDetailActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr int SUMMARY_CARD_HEIGHT = 70;
constexpr int SUMMARY_GAP = 8;

std::string getBookTitle(const ReadingBookStats& book) { return book.title.empty() ? book.path : book.title; }

void drawMetricCard(const GfxRenderer& renderer, const Rect& rect, const char* label, const std::string& value) {
  AppMetricCard::draw(renderer, rect, label, value);
}
}  // namespace

ReadingDayDetailActivity::ReadingDayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const uint32_t dayOrdinal)
    : UiListActivity("ReadingDayDetail", renderer, mappedInput), dayOrdinal(dayOrdinal) {}

void ReadingDayDetailActivity::refreshEntries() {
  entries = ReadingStatsAnalytics::getBooksReadOnDay(dayOrdinal);
  nav.selected = std::min(nav.selected, std::max(0, listCount() - 1));
  nav.scrollBy(0, listCount());
  nav.follow(listCount());

  rowValues.clear();
  rowValues.reserve(entries.size());
  std::transform(entries.begin(), entries.end(), std::back_inserter(rowValues),
                 [](const auto& entry) { return ReadingStatsAnalytics::formatDurationHm(entry.readingMs); });

  rowItems.clear();
  rowItems.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const ReadingBookStats* book = entries[i].book;
    fui::ListItem item;
    item.label = book ? (book->title.empty() ? book->path.c_str() : book->title.c_str()) : tr(STR_NOT_SET);
    item.subtitle = book ? (book->author.empty() ? tr(STR_IN_PROGRESS) : book->author.c_str()) : tr(STR_NOT_SET);
    item.icon = listIconFor(UIIcon::Book, 32);
    item.value = rowValues[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  dateLabel = ReadingStatsAnalytics::formatDayOrdinalLabel(dayOrdinal);
  const auto timeline = ReadingStatsAnalytics::buildTimelineDayEntry(dayOrdinal);
  totalReadingText = ReadingStatsAnalytics::formatDurationHm(entries.empty() ? 0 : timeline.totalReadingMs);
  bookCountText = std::to_string(entries.size());
  topBookTitle = !entries.empty() && entries.front().book != nullptr ? getBookTitle(*entries.front().book)
                                                                     : std::string(tr(STR_NOT_SET));
}

void ReadingDayDetailActivity::openSelectedBook() {
  if (nav.selected < 0 || nav.selected >= listCount() || entries[nav.selected].book == nullptr) {
    return;
  }

  app.clearTapFlash();
  startActivityForResultWith<ReadingStatsDetailActivity>(
      [this](const ActivityResult&) {
        refreshEntries();
        requestUpdate();
      },
      entries[nav.selected].book->path);
}

void ReadingDayDetailActivity::onEnter() {
  UiListActivity::onEnter();
  refreshEntries();
}

void ReadingDayDetailActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  openSelectedBook();
}

void ReadingDayDetailActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int sidePadding = metrics.contentSidePadding;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardWidth = (pageWidth - sidePadding * 2 - SUMMARY_GAP) / 2;

  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_READING_DAY), dateLabel.c_str());

  drawMetricCard(renderer, Rect{sidePadding, contentTop, cardWidth, SUMMARY_CARD_HEIGHT}, tr(STR_TOTAL_TIME),
                 totalReadingText);
  drawMetricCard(renderer, Rect{sidePadding + cardWidth + SUMMARY_GAP, contentTop, cardWidth, SUMMARY_CARD_HEIGHT},
                 tr(STR_BOOKS_READ), bookCountText);

  const int listTop = contentTop + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  GUI.drawSubHeader(renderer, Rect{0, listTop, pageWidth, 34}, tr(STR_TOP_BOOK), topBookTitle.c_str());
}

void ReadingDayDetailActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listTop = contentTop + SUMMARY_CARD_HEIGHT + metrics.verticalSpacing;
  const int listContentTop = listTop + 34 + 10;
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(listContentTop), static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), static_cast<int16_t>(safe.x)});

  if (entries.empty()) {
    screen.centeredText(tr(STR_NO_READING_DAY), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void ReadingDayDetailActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
