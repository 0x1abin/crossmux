#include "AppsMenuActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "../../components/UITheme.h"
#include "../../components/UiAppHelpers.h"
#include "../../components/icons/inx_apps.h"
#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "OpdsServerStore.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

// Single source of truth for the Apps menu — add a new app here, then provide the
// matching `goTo<App>()` in ActivityManager and assign a stable, never-reused AppId.
enum class AppId : uint8_t {
  ReadingStats = 0,
  WeRead = 1,
  Sudoku = 2,
  Gomoku = 3,
  ChineseChess = 4,
  Minesweeper = 5,
  Game2048 = 6,
  UglyAvatar = 7,
  Standby = 8,
  AirPage = 9,
  Buddy = 10,
  Sokoban = 11,
  PixelSwitch = 12,
  FileTransfer = 13,
  OpdsBrowser = 14,
  Calculator = 15,
  Woodfish = 16,
  Count = 17,
};

struct AppEntry {
  AppId id;
  StrId titleId;
  UIIcon icon;
  void (ActivityManager::*open)();
};

constexpr AppEntry kAppEntries[] = {
    {AppId::FileTransfer, StrId::STR_FILE_TRANSFER, UIIcon::Transfer, &ActivityManager::goToFileTransfer},
    {AppId::OpdsBrowser, StrId::STR_OPDS_BROWSER, UIIcon::Opds, &ActivityManager::goToBrowser},
#ifdef ENABLE_CHINESE_VERSION
    {AppId::WeRead, StrId::STR_WEREAD_TITLE, UIIcon::WeRead, &ActivityManager::goToWeRead},
#endif
    {AppId::AirPage, StrId::STR_AIRPAGE_TITLE, UIIcon::AirPage, &ActivityManager::goToAirPage},
    {AppId::ReadingStats, StrId::STR_READING_STATS, UIIcon::ReadingStats, &ActivityManager::goToReadingStatsMenu},
    {AppId::Sudoku, StrId::STR_SUDOKU_TITLE, UIIcon::Sudoku, &ActivityManager::goToSudoku},
    {AppId::Gomoku, StrId::STR_GOMOKU_TITLE, UIIcon::Gomoku, &ActivityManager::goToGomoku},
    {AppId::Sokoban, StrId::STR_SOKOBAN_TITLE, UIIcon::Sokoban, &ActivityManager::goToSokoban},
#ifdef ENABLE_CHINESE_VERSION
    {AppId::ChineseChess, StrId::STR_CHINESE_CHESS_TITLE, UIIcon::ChineseChess, &ActivityManager::goToChineseChess},
#endif
    {AppId::Minesweeper, StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {AppId::Game2048, StrId::STR_2048_TITLE, UIIcon::Game2048, &ActivityManager::goToGame2048},
    {AppId::UglyAvatar, StrId::STR_UGLY_AVATAR, UIIcon::Avatar, &ActivityManager::goToUglyAvatar},
    {AppId::Buddy, StrId::STR_BUDDY_TITLE, UIIcon::Buddy, &ActivityManager::goToBuddy},
    {AppId::PixelSwitch, StrId::STR_PIXEL_SWITCH_TITLE, UIIcon::PixelSwitch, &ActivityManager::goToPixelSwitch},
    {AppId::Calculator, StrId::STR_CALCULATOR_TITLE, UIIcon::Calculator, &ActivityManager::goToCalculator},
    {AppId::Woodfish, StrId::STR_WOODFISH_TITLE, UIIcon::Woodfish, &ActivityManager::goToWoodfish},
    {AppId::Standby, StrId::STR_STANDBY_TITLE, UIIcon::Standby, &ActivityManager::goToStandby},
};

constexpr int kAppCount = static_cast<int>(sizeof(kAppEntries) / sizeof(kAppEntries[0]));

constexpr uint32_t appBit(const AppId id) { return uint32_t{1} << static_cast<uint8_t>(id); }

constexpr int visibleAppCount(const uint32_t hiddenMask) {
  int count = 0;
  for (const auto& app : kAppEntries) {
    if ((hiddenMask & appBit(app.id)) == 0) {
      // cppcheck-suppress useStlAlgorithm
      ++count;
    }
  }
  return count;
}

constexpr int appIndexForVisibleIndex(const uint32_t hiddenMask, const int visibleIndex) {
  int visible = 0;
  for (int appIndex = 0; appIndex < kAppCount; ++appIndex) {
    if ((hiddenMask & appBit(kAppEntries[appIndex].id)) != 0) continue;
    if (visible++ == visibleIndex) return appIndex;
  }
  return -1;
}

constexpr uint32_t effectiveHiddenMask(const uint32_t hiddenMask, const bool hasOpdsServers,
                                       const CrossPointSettings::ContentProfile profile) {
  uint32_t effective = hiddenMask;
  if (!hasOpdsServers) effective |= appBit(AppId::OpdsBrowser);
  if (profile == CrossPointSettings::ContentProfile::Global) effective |= CrossPointSettings::CHINA_ONLY_APPS_MASK;
  return effective;
}

constexpr bool appIdsAreUnique() {
  for (int i = 0; i < kAppCount; ++i) {
    for (int j = i + 1; j < kAppCount; ++j) {
      if (kAppEntries[i].id == kAppEntries[j].id) return false;
    }
  }
  return true;
}

static_assert(kAppCount <= 32, "the app catalog must fit hiddenAppsMask");
static_assert(static_cast<uint8_t>(AppId::Count) <= 32, "hiddenAppsMask supports at most 32 stable app IDs");
static_assert(static_cast<uint8_t>(AppId::Buddy) == CrossPointSettings::BUDDY_APP_ID,
              "the Buddy app ID must remain stable");
static_assert(static_cast<uint8_t>(AppId::PixelSwitch) == CrossPointSettings::PIXEL_SWITCH_APP_ID,
              "the Pixel Switch app ID must remain stable");
static_assert(static_cast<uint8_t>(AppId::Calculator) == 15, "the Calculator app ID must remain stable");
static_assert(static_cast<uint8_t>(AppId::Woodfish) == 16, "the Woodfish app ID must remain stable");
static_assert(appBit(AppId::Woodfish) == (uint32_t{1} << 16), "Woodfish visibility must use the first widened bit");
static_assert(appIdsAreUnique(), "stable app IDs must not be reused");
static_assert(CrossPointSettings::DEFAULT_HIDDEN_APPS_MASK ==
                  (appBit(AppId::ChineseChess) | appBit(AppId::Minesweeper) | appBit(AppId::Game2048) |
                   appBit(AppId::Standby) | appBit(AppId::Buddy) | appBit(AppId::PixelSwitch)),
              "the default mask must hide Chinese chess, Minesweeper, 2048, Standby, Buddy, and Pixel Switch");
static_assert(visibleAppCount(0) == kAppCount, "a zero mask must show every compiled app");
static_assert(visibleAppCount(UINT32_MAX) == 0, "a full mask must hide every compiled app");
static_assert(visibleAppCount(appBit(AppId::Woodfish)) == kAppCount - 1, "the widened mask must hide Woodfish");
static_assert(visibleAppCount(effectiveHiddenMask(0, false, CrossPointSettings::ContentProfile::China)) ==
                  kAppCount - 1,
              "OPDS must be hidden when no server is configured");
static_assert(visibleAppCount(effectiveHiddenMask(0, true, CrossPointSettings::ContentProfile::Global)) ==
                  kAppCount - 2,
              "global profile must hide the two China-only apps");
static_assert(appIndexForVisibleIndex(appBit(kAppEntries[1].id), 1) == 2,
              "visible indices must skip a hidden middle app");

}  // namespace

int AppsMenuActivity::getAppCount() { return kAppCount; }

StrId AppsMenuActivity::getAppTitleId(const int appIndex) {
  return appIndex >= 0 && appIndex < kAppCount ? kAppEntries[appIndex].titleId : StrId::STR_NONE_OPT;
}

bool AppsMenuActivity::isAppVisible(const int appIndex) {
  if (appIndex < 0 || appIndex >= kAppCount) return false;
  const uint32_t hidden = effectiveHiddenMask(SETTINGS.hiddenAppsMask, true, SETTINGS.contentProfile);
  return (hidden & appBit(kAppEntries[appIndex].id)) == 0;
}

bool AppsMenuActivity::setAppVisible(const int appIndex, const bool visible) {
  if (appIndex < 0 || appIndex >= kAppCount) return false;

  const uint32_t bit = appBit(kAppEntries[appIndex].id);
  const uint32_t updatedMask = visible ? SETTINGS.hiddenAppsMask & ~bit : SETTINGS.hiddenAppsMask | bit;
  if (updatedMask == SETTINGS.hiddenAppsMask) return false;

  SETTINGS.hiddenAppsMask = updatedMask;
  return true;
}

int AppsMenuActivity::getVisibleAppCount() {
  return visibleAppCount(
      effectiveHiddenMask(SETTINGS.hiddenAppsMask, OPDS_STORE.hasServers(), SETTINGS.contentProfile));
}

void AppsMenuActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  nav.selected = MainTabs::contentEdgeIndex(edge, getVisibleAppCount());
  nav.follow(getVisibleAppCount());
}

int AppsMenuActivity::getAppIndexForVisibleIndex(const int visibleIndex) {
  return appIndexForVisibleIndex(
      effectiveHiddenMask(SETTINGS.hiddenAppsMask, OPDS_STORE.hasServers(), SETTINGS.contentProfile), visibleIndex);
}

void AppsMenuActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRowItems();
}

void AppsMenuActivity::rebuildRowItems() {
  rowItems.clear();
  rowItems.reserve(static_cast<size_t>(getVisibleAppCount()));
  for (int visibleIndex = 0; visibleIndex < getVisibleAppCount(); ++visibleIndex) {
    const int appIndex = getAppIndexForVisibleIndex(visibleIndex);
    if (appIndex < 0) continue;
    fui::ListItem item;
    item.label = I18N.get(kAppEntries[appIndex].titleId);
    item.icon = listIconFor(kAppEntries[appIndex].icon, 32);
    item.actionValue = static_cast<int16_t>(visibleIndex);
    rowItems.push_back(item);
  }
}

bool AppsMenuActivity::usesIconLayout() const {
  return UITheme::getInstance().hasMainTabs() &&
         InxGridGeometry::layoutFrom(SETTINGS.inxAppsLayout) == InxItemLayout::Icons;
}

int AppsMenuActivity::iconIndexFromPoint(const int x, const int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return InxGridGeometry::indexFromPoint(x, y - top, renderer.getScreenWidth(), height,
                                         InxGridGeometry::pageStart(nav.selected, getVisibleAppCount()),
                                         getVisibleAppCount());
}

void AppsMenuActivity::openSelected() {
  const int appIndex = getAppIndexForVisibleIndex(nav.selected);
  if (appIndex >= 0) (activityManager.*kAppEntries[appIndex].open)();
}

void AppsMenuActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  openSelected();
}

bool AppsMenuActivity::handleCustomInput() {
  if (!usesIconLayout()) return false;

  const int visibleCount = getVisibleAppCount();
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    const int touched = iconIndexFromPoint(x, y);
    if (touched >= 0 && touched != nav.selected) {
      nav.selected = touched;
      requestUpdate();
    }
    return true;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    const int touched = iconIndexFromPoint(x, y);
    if (touched >= 0) {
      nav.selected = touched;
      openSelected();
    }
    return true;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    nav.selected = ButtonNavigator::nextPageIndex(nav.selected, visibleCount, InxGridGeometry::itemsPerPage);
    requestUpdate();
    return true;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    nav.selected = ButtonNavigator::previousPageIndex(nav.selected, visibleCount, InxGridGeometry::itemsPerPage);
    requestUpdate();
    return true;
  }
  return false;
}

void AppsMenuActivity::drawIconGrid(const Rect& rect, const int visibleCount, const bool showSelection) const {
  const int start = InxGridGeometry::pageStart(nav.selected, visibleCount);
  const int cellWidth = rect.width / InxGridGeometry::columns;
  const int cellHeight = rect.height / InxGridGeometry::rows;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int iconScale = 2;
  constexpr int iconSize = InxAppIcons::size * iconScale;

  for (int slot = 0; slot < InxGridGeometry::itemsPerPage && start + slot < visibleCount; ++slot) {
    const int visibleIndex = start + slot;
    const int appIndex = getAppIndexForVisibleIndex(visibleIndex);
    if (appIndex < 0) continue;
    const int column = slot % InxGridGeometry::columns;
    const int row = slot / InxGridGeometry::columns;
    const Rect cell{rect.x + column * cellWidth + 4, rect.y + row * cellHeight + 4, cellWidth - 8, cellHeight - 8};
    const bool isSelected = showSelection && visibleIndex == nav.selected;
    if (isSelected) renderer.fillRect(cell.x, cell.y, cell.width, cell.height, true);

    const int iconX = cell.x + (cell.width - iconSize) / 2;
    const int iconY = cell.y + std::max(5, (cell.height - iconSize - lineHeight - 8) / 2);
    InxAppIcons::draw(renderer, kAppEntries[appIndex].icon, iconX, iconY, iconScale, isSelected);
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, I18N.get(kAppEntries[appIndex].titleId), std::max(1, cell.width - 8));
    const int labelX = cell.x + (cell.width - renderer.getTextWidth(UI_10_FONT_ID, label.c_str())) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, iconY + iconSize + 8, label.c_str(), !isSelected);
  }

  GUI.drawSideScrollBar(renderer, rect, visibleCount, start, InxGridGeometry::itemsPerPage);
}

void AppsMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleCount = getVisibleAppCount();
  const bool showSelection = showMainTabContentSelection();

  if (visibleCount == 0) {
    UITheme::drawCenteredWrappedText(renderer, Rect{0, listY, sw, listH}, UI_12_FONT_ID, tr(STR_NO_APPS_ENABLED), 2);
  } else if (usesIconLayout()) {
    drawIconGrid(Rect{0, listY, sw, listH}, visibleCount, showSelection);
  } else {
    const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    screen.setContentMargin(fui::Insets{static_cast<int16_t>(listY), static_cast<int16_t>(sw - (safe.x + safe.width)),
                                        static_cast<int16_t>(sh - (safe.y + safe.height)),
                                        static_cast<int16_t>(safe.x)});
    fui::ListProps props;
    props.items = rowItems.data();
    props.count = static_cast<uint16_t>(rowItems.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    syncListViewport(screen, props);
    screen.list(props);
  }
}

void AppsMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  drawPageHeader(Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, tr(STR_APPS_TITLE));
}

void AppsMenuActivity::drawFooter() {
  const int visibleCount = getVisibleAppCount();
  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_SELECT), visibleCount > 1);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AppsMenuActivity::onBackButton() { activityManager.goHome(); }
