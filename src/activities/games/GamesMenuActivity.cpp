#include "GamesMenuActivity.h"

#include <I18n.h>

#include <string>

#include "../../components/UITheme.h"

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  buildItems();
  selected = 0;
  requestUpdate();
}

void GamesMenuActivity::onExit() { Activity::onExit(); }

void GamesMenuActivity::buildItems() {
  items.clear();
  items.reserve(1);
  items.push_back(GameKind::Sudoku);
}

void GamesMenuActivity::loop() {
  const int n = static_cast<int>(items.size());
  buttonNavigator.onNext([this, n] {
    selected = ButtonNavigator::nextIndex(selected, n);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, n] {
    selected = ButtonNavigator::previousIndex(selected, n);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelect();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void GamesMenuActivity::onSelect() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  switch (items[selected]) {
    case GameKind::Sudoku:
      activityManager.goToSudoku();
      return;
  }
}

void GamesMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GAMES_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    switch (items[i]) {
      case GameKind::Sudoku:
        return std::string(tr(STR_SUDOKU_TITLE));
    }
    return "";
  };

  auto rowSubtitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    switch (items[i]) {
      case GameKind::Sudoku:
        return std::string(tr(STR_SUDOKU_SUBTITLE));
    }
    return "";
  };

  auto rowIcon = [this](int i) -> UIIcon {
    if (i < 0 || i >= static_cast<int>(items.size())) return UIIcon::Sudoku;
    switch (items[i]) {
      case GameKind::Sudoku:
        return UIIcon::Sudoku;
    }
    return UIIcon::Sudoku;
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, static_cast<int>(items.size()), selected, rowTitle, rowSubtitle,
               rowIcon);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
