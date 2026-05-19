#include "AppsMenuActivity.h"

#include <I18n.h>

#include <string>

#include "../../components/UITheme.h"

namespace {

// Single source of truth for the Apps menu — add a new app here, then provide the
// matching `goTo<App>()` in ActivityManager. The enum-and-switch pattern this replaces
// required edits in five places per new app; this requires one.
struct AppEntry {
  StrId titleId;
  UIIcon icon;
  void (ActivityManager::*open)();
};

constexpr AppEntry kAppEntries[] = {
    {StrId::STR_SUDOKU_TITLE, UIIcon::Sudoku, &ActivityManager::goToSudoku},
    {StrId::STR_GOMOKU_TITLE, UIIcon::Gomoku, &ActivityManager::goToGomoku},
#ifdef ENABLE_CHINESE_VERSION
    {StrId::STR_CHINESE_CHESS_TITLE, UIIcon::ChineseChess, &ActivityManager::goToChineseChess},
    {StrId::STR_WEREAD_TITLE, UIIcon::WeRead, &ActivityManager::goToWeRead},
#endif
    {StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {StrId::STR_2048_TITLE, UIIcon::Game2048, &ActivityManager::goToGame2048},
    {StrId::STR_UGLY_AVATAR, UIIcon::Avatar, &ActivityManager::goToUglyAvatar},
    {StrId::STR_CELLULAR_TITLE, UIIcon::Cellular, &ActivityManager::goToCellular},
    {StrId::STR_STANDBY_TITLE, UIIcon::Standby, &ActivityManager::goToStandby},
};

constexpr int kAppCount = static_cast<int>(sizeof(kAppEntries) / sizeof(kAppEntries[0]));

}  // namespace

void AppsMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}

void AppsMenuActivity::onExit() { Activity::onExit(); }

void AppsMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selected = ButtonNavigator::nextIndex(selected, kAppCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selected = ButtonNavigator::previousIndex(selected, kAppCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected >= 0 && selected < kAppCount) {
      (activityManager.*kAppEntries[selected].open)();
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

  GUI.drawButtonMenu(
      renderer, Rect{0, listY, sw, listH}, kAppCount, selected,
      [](int i) { return std::string(I18n::getInstance().get(kAppEntries[i].titleId)); },
      [](int i) { return kAppEntries[i].icon; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
