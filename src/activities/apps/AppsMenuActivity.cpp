#include "AppsMenuActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "../../components/UITheme.h"
#include "../../util/PaginationDots.h"
#include "CrossPointSettings.h"
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
  Count = 13,
};

struct AppEntry {
  AppId id;
  StrId titleId;
  UIIcon icon;
  void (ActivityManager::*open)();
};

constexpr AppEntry kAppEntries[] = {
#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)
    {AppId::WeRead, StrId::STR_WEREAD_TITLE, UIIcon::WeRead, &ActivityManager::goToWeRead},
#endif
    {AppId::ReadingStats, StrId::STR_READING_STATS, UIIcon::Library, &ActivityManager::goToReadingStatsMenu},
    {AppId::Sudoku, StrId::STR_SUDOKU_TITLE, UIIcon::Sudoku, &ActivityManager::goToSudoku},
    {AppId::Gomoku, StrId::STR_GOMOKU_TITLE, UIIcon::Gomoku, &ActivityManager::goToGomoku},
    {AppId::Sokoban, StrId::STR_SOKOBAN_TITLE, UIIcon::Sokoban, &ActivityManager::goToSokoban},
#ifdef ENABLE_CHINESE_VERSION
    {AppId::ChineseChess, StrId::STR_CHINESE_CHESS_TITLE, UIIcon::ChineseChess, &ActivityManager::goToChineseChess},
#endif
    {AppId::Minesweeper, StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {AppId::Game2048, StrId::STR_2048_TITLE, UIIcon::Game2048, &ActivityManager::goToGame2048},
    {AppId::UglyAvatar, StrId::STR_UGLY_AVATAR, UIIcon::Avatar, &ActivityManager::goToUglyAvatar},
    {AppId::AirPage, StrId::STR_AIRPAGE_TITLE, UIIcon::Wifi, &ActivityManager::goToAirPage},
    {AppId::Buddy, StrId::STR_BUDDY_TITLE, UIIcon::Buddy, &ActivityManager::goToBuddy},
    {AppId::PixelSwitch, StrId::STR_PIXEL_SWITCH_TITLE, UIIcon::Image, &ActivityManager::goToPixelSwitch},
    {AppId::Standby, StrId::STR_STANDBY_TITLE, UIIcon::Standby, &ActivityManager::goToStandby},
};

constexpr int kAppCount = static_cast<int>(sizeof(kAppEntries) / sizeof(kAppEntries[0]));

constexpr uint16_t appBit(const AppId id) { return uint16_t{1} << static_cast<uint8_t>(id); }

constexpr int visibleAppCount(const uint16_t hiddenMask) {
  int count = 0;
  for (const auto& app : kAppEntries) {
    if ((hiddenMask & appBit(app.id)) == 0) {
      // cppcheck-suppress useStlAlgorithm
      ++count;
    }
  }
  return count;
}

constexpr int appIndexForVisibleIndex(const uint16_t hiddenMask, const int visibleIndex) {
  int visible = 0;
  for (int appIndex = 0; appIndex < kAppCount; ++appIndex) {
    if ((hiddenMask & appBit(kAppEntries[appIndex].id)) != 0) continue;
    if (visible++ == visibleIndex) return appIndex;
  }
  return -1;
}

constexpr bool appIdsAreUnique() {
  for (int i = 0; i < kAppCount; ++i) {
    for (int j = i + 1; j < kAppCount; ++j) {
      if (kAppEntries[i].id == kAppEntries[j].id) return false;
    }
  }
  return true;
}

static_assert(kAppCount <= 16, "the app catalog must fit hiddenAppsMask");
static_assert(static_cast<uint8_t>(AppId::Count) <= 16, "hiddenAppsMask supports at most 16 stable app IDs");
static_assert(static_cast<uint8_t>(AppId::Buddy) == CrossPointSettings::BUDDY_APP_ID,
              "the Buddy app ID must remain stable");
static_assert(appIdsAreUnique(), "stable app IDs must not be reused");
static_assert(CrossPointSettings::DEFAULT_HIDDEN_APPS_MASK ==
                  (appBit(AppId::ChineseChess) | appBit(AppId::Minesweeper) | appBit(AppId::Game2048) |
                   appBit(AppId::Buddy)),
              "the default mask must hide Chinese chess, Minesweeper, 2048, and Buddy");
static_assert(visibleAppCount(0) == kAppCount, "a zero mask must show every compiled app");
static_assert(visibleAppCount(UINT16_MAX) == 0, "a full mask must hide every compiled app");
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

  const uint16_t bit = appBit(kAppEntries[appIndex].id);
  const uint16_t updatedMask =
      visible ? static_cast<uint16_t>(SETTINGS.hiddenAppsMask & ~bit) : SETTINGS.hiddenAppsMask | bit;
  if (updatedMask == SETTINGS.hiddenAppsMask) return false;

  SETTINGS.hiddenAppsMask = updatedMask;
  return true;
}

int AppsMenuActivity::getVisibleAppCount() { return visibleAppCount(SETTINGS.hiddenAppsMask); }

int AppsMenuActivity::getAppIndexForVisibleIndex(const int visibleIndex) {
  return appIndexForVisibleIndex(SETTINGS.hiddenAppsMask, visibleIndex);
}

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}

void AppsMenuActivity::onExit() { Activity::onExit(); }

void AppsMenuActivity::loop() {
  const int visibleCount = getVisibleAppCount();
  buttonNavigator.onNext([this, visibleCount] {
    selected = ButtonNavigator::nextIndex(selected, visibleCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, visibleCount] {
    selected = ButtonNavigator::previousIndex(selected, visibleCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int appIndex = getAppIndexForVisibleIndex(selected);
    if (appIndex >= 0) {
      (activityManager.*kAppEntries[appIndex].open)();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void AppsMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_APPS_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int visibleCount = getVisibleAppCount();

  if (visibleCount == 0) {
    UITheme::drawCenteredWrappedText(renderer, Rect{0, listY, sw, listH}, UI_12_FONT_ID, tr(STR_NO_APPS_ENABLED), 2);
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

    // ponytail: scan at most 16 entries instead of keeping a RAM-backed filtered list.
    GUI.drawButtonMenu(
        renderer, Rect{0, listY, sw, listH}, pageCount, selected - pageStart,
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
      const int dotsY = listY + listH - 8;
      drawPaginationDots(renderer, sw, dotsY, totalPages, page);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
