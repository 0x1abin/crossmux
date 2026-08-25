#include "AppsMenuActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "../../components/UITheme.h"
#include "../../components/icons/inx_apps.h"
#include "../../util/PaginationDots.h"
#include "CrossPointSettings.h"
#include "InxItemLayout.h"
#include "OpdsServerStore.h"
#include "fontIds.h"

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

constexpr uint32_t effectiveHiddenMask(const uint32_t hiddenMask, const bool hasOpdsServers) {
  return hasOpdsServers ? hiddenMask : hiddenMask | appBit(AppId::OpdsBrowser);
}

constexpr bool appIdsAreUnique() {
  for (int i = 0; i < kAppCount; ++i) {
    for (int j = i + 1; j < kAppCount; ++j) {
      if (kAppEntries[i].id == kAppEntries[j].id) return false;
    }
  }
  return true;
}

constexpr bool usesSideScrollBar(const CrossPointSettings::UI_THEME theme) {
  switch (theme) {
    case CrossPointSettings::LYRA:
    case CrossPointSettings::LYRA_3_COVERS:
    case CrossPointSettings::LYRA_CAROUSEL:
    case CrossPointSettings::INX:
      return true;
    case CrossPointSettings::CLASSIC:
    case CrossPointSettings::ROUNDEDRAFF:
      return false;
  }
  return false;
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
static_assert(visibleAppCount(effectiveHiddenMask(0, false)) == kAppCount - 1,
              "OPDS must be hidden when no server is configured");
static_assert(appIndexForVisibleIndex(appBit(kAppEntries[1].id), 1) == 2,
              "visible indices must skip a hidden middle app");

}  // namespace

int AppsMenuActivity::getAppCount() { return kAppCount; }

StrId AppsMenuActivity::getAppTitleId(const int appIndex) {
  return appIndex >= 0 && appIndex < kAppCount ? kAppEntries[appIndex].titleId : StrId::STR_NONE_OPT;
}

bool AppsMenuActivity::isAppVisible(const int appIndex) {
  return appIndex >= 0 && appIndex < kAppCount && (SETTINGS.hiddenAppsMask & appBit(kAppEntries[appIndex].id)) == 0;
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
  return visibleAppCount(effectiveHiddenMask(SETTINGS.hiddenAppsMask, OPDS_STORE.hasServers()));
}

void AppsMenuActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selected = MainTabs::contentEdgeIndex(edge, getVisibleAppCount());
}

int AppsMenuActivity::getAppIndexForVisibleIndex(const int visibleIndex) {
  return appIndexForVisibleIndex(effectiveHiddenMask(SETTINGS.hiddenAppsMask, OPDS_STORE.hasServers()), visibleIndex);
}

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}

void AppsMenuActivity::onExit() { Activity::onExit(); }

bool AppsMenuActivity::usesIconLayout() const {
  return UITheme::getInstance().hasMainTabs() &&
         InxGridGeometry::layoutFrom(SETTINGS.inxAppsLayout) == InxItemLayout::Icons;
}

int AppsMenuActivity::iconIndexFromPoint(const int x, const int y) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return InxGridGeometry::indexFromPoint(x, y - top, renderer.getScreenWidth(), height,
                                         InxGridGeometry::pageStart(selected, getVisibleAppCount()),
                                         getVisibleAppCount());
}

void AppsMenuActivity::openSelected() {
  const int appIndex = getAppIndexForVisibleIndex(selected);
  if (appIndex >= 0) (activityManager.*kAppEntries[appIndex].open)();
}

void AppsMenuActivity::loop() {
  const int visibleCount = getVisibleAppCount();
  if (usesIconLayout()) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTouchDown(x, y)) {
      const int touched = iconIndexFromPoint(x, y);
      if (touched >= 0 && touched != selected) {
        selected = touched;
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      const int touched = iconIndexFromPoint(x, y);
      if (touched >= 0) {
        selected = touched;
        openSelected();
      }
      return;
    }
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selected = ButtonNavigator::nextPageIndex(selected, visibleCount, InxGridGeometry::itemsPerPage);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selected = ButtonNavigator::previousPageIndex(selected, visibleCount, InxGridGeometry::itemsPerPage);
      requestUpdate();
      return;
    }
  }

  buttonNavigator.onNext([this, visibleCount] {
    selected = ButtonNavigator::nextIndex(selected, visibleCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, visibleCount] {
    selected = ButtonNavigator::previousIndex(selected, visibleCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void AppsMenuActivity::drawIconGrid(const Rect& rect, const int visibleCount, const bool showSelection) const {
  const int start = InxGridGeometry::pageStart(selected, visibleCount);
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
    const bool isSelected = showSelection && visibleIndex == selected;
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

void AppsMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  drawPageHeader(Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_APPS_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleCount = getVisibleAppCount();
  const auto theme = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  const bool showSelection = showMainTabContentSelection();

  if (visibleCount == 0) {
    UITheme::drawCenteredWrappedText(renderer, Rect{0, listY, sw, listH}, UI_12_FONT_ID, tr(STR_NO_APPS_ENABLED), 2);
  } else if (usesIconLayout()) {
    drawIconGrid(Rect{0, listY, sw, listH}, visibleCount, showSelection);
  } else {
    // Halved inter-row gap (8 -> 4 on LYRA) keeps the home-tile look but tightens the list.
    const int spacing = metrics.menuSpacing / 2;
    const int rowStep = metrics.menuRowHeight + spacing;
    // Number of rows that fit: n rows occupy n*rowHeight + (n-1)*spacing <= listH.
    const int perPage = std::max(1, (listH + spacing) / rowStep);
    const int totalPages = (visibleCount + perPage - 1) / perPage;
    const int page = selected / perPage;
    const int pageStart = page * perPage;
    const int pageCount = std::min(perPage, visibleCount - pageStart);

    // ponytail: scan at most 32 entries instead of keeping a RAM-backed filtered list.
    GUI.drawButtonMenu(
        renderer, Rect{0, listY, sw, listH}, pageCount, showSelection ? selected - pageStart : -1,
        [pageStart](int i) {
          const int appIndex = getAppIndexForVisibleIndex(i + pageStart);
          return appIndex >= 0 ? std::string(I18N.get(kAppEntries[appIndex].titleId)) : std::string();
        },
        [pageStart](int i) {
          const int appIndex = getAppIndexForVisibleIndex(i + pageStart);
          return appIndex >= 0 ? kAppEntries[appIndex].icon : UIIcon::None;
        },
        spacing);

    if (totalPages > 1) {
      if (usesSideScrollBar(theme)) {
        GUI.drawSideScrollBar(renderer, Rect{0, listY, sw, listH}, visibleCount, pageStart, perPage);
      } else {
        const int dotsY = listY + listH - 8;
        drawPaginationDots(renderer, sw, dotsY, totalPages, page);
      }
    }
  }

  const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_SELECT), visibleCount > 1);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
