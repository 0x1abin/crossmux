#include "SudokuMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "SudokuGameActivity.h"

SudokuMenuActivity::SudokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("SudokuMenu", renderer, mappedInput) {}

void SudokuMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  buildItems();
  selected = 0;
  showingStats = false;
  cachedStats = SudokuStore::loadStats();
  requestUpdate();
}

void SudokuMenuActivity::onExit() { Activity::onExit(); }

void SudokuMenuActivity::buildItems() {
  items.clear();
  items.reserve(5);

  if (SudokuStore::hasInProgress()) {
    SudokuSaveSlot slot;
    if (SudokuStore::load(slot)) {
      resumeElapsedSec = slot.elapsedSec;
      resumeDifficulty = slot.difficulty;
      const uint8_t filled = slot.board.countFilled();
      uint8_t fixedCount = 0;
      for (int i = 0; i < 81; i++) {
        if (slot.board.fixed[i]) fixedCount++;
      }
      const uint8_t blanks = static_cast<uint8_t>(81 - fixedCount);
      const uint8_t userFilled = static_cast<uint8_t>(filled - fixedCount);
      resumeProgressPercent = blanks > 0 ? static_cast<uint8_t>((userFilled * 100u) / blanks) : 100;
      items.push_back({ItemKind::Continue, slot.difficulty});
    }
  }

  items.push_back({ItemKind::NewGame, SudokuBoard::Difficulty::Easy});
  items.push_back({ItemKind::NewGame, SudokuBoard::Difficulty::Medium});
  items.push_back({ItemKind::NewGame, SudokuBoard::Difficulty::Hard});
  items.push_back({ItemKind::Stats, SudokuBoard::Difficulty::Easy});
}

void SudokuMenuActivity::loop() {
  if (showingStats) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingStats = false;
      requestUpdate();
    }
    return;
  }

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
    activityManager.goToGames();
  }
}

void SudokuMenuActivity::onSelect() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  const Item& it = items[selected];
  switch (it.kind) {
    case ItemKind::Continue:
      activityManager.replaceActivity(std::make_unique<SudokuGameActivity>(renderer, mappedInput, it.difficulty, true));
      return;
    case ItemKind::NewGame:
      SudokuStore::clear();
      activityManager.replaceActivity(
          std::make_unique<SudokuGameActivity>(renderer, mappedInput, it.difficulty, false));
      return;
    case ItemKind::Stats:
      showingStats = true;
      requestUpdate();
      return;
  }
}

void SudokuMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  if (showingStats) {
    renderStats();
  } else {
    renderList();
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SudokuMenuActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_SUDOKU_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    switch (it.kind) {
      case ItemKind::Continue:
        return std::string(tr(STR_SUDOKU_CONTINUE));
      case ItemKind::NewGame:
        if (it.difficulty == SudokuBoard::Difficulty::Medium) return std::string(tr(STR_SUDOKU_NEW_MEDIUM));
        if (it.difficulty == SudokuBoard::Difficulty::Hard) return std::string(tr(STR_SUDOKU_NEW_HARD));
        return std::string(tr(STR_SUDOKU_NEW_EASY));
      case ItemKind::Stats:
        return std::string(tr(STR_SUDOKU_STATS));
    }
    return "";
  };

  auto rowSubtitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    char buf[80];
    switch (it.kind) {
      case ItemKind::Continue: {
        snprintf(buf, sizeof(buf), "%s · %02u:%02u · %u%%", SudokuGameActivity::difficultyName(resumeDifficulty),
                 static_cast<unsigned>(resumeElapsedSec / 60), static_cast<unsigned>(resumeElapsedSec % 60),
                 static_cast<unsigned>(resumeProgressPercent));
        return std::string(buf);
      }
      case ItemKind::NewGame:
        if (it.difficulty == SudokuBoard::Difficulty::Medium) return std::string(tr(STR_SUDOKU_MEDIUM_DESC));
        if (it.difficulty == SudokuBoard::Difficulty::Hard) return std::string(tr(STR_SUDOKU_HARD_DESC));
        return std::string(tr(STR_SUDOKU_EASY_DESC));
      case ItemKind::Stats:
        if (cachedStats.bestTimeSec[0] > 0) {
          snprintf(buf, sizeof(buf), "%s %02u:%02u · %u %s", tr(STR_SUDOKU_DIFFICULTY_EASY),
                   static_cast<unsigned>(cachedStats.bestTimeSec[0] / 60),
                   static_cast<unsigned>(cachedStats.bestTimeSec[0] % 60),
                   static_cast<unsigned>(cachedStats.completedCount[0]), tr(STR_SUDOKU_COMPLETED));
          return std::string(buf);
        }
        return std::string(tr(STR_SUDOKU_NO_RECORD));
    }
    return "";
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, static_cast<int>(items.size()), selected, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SudokuMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_SUDOKU_STATS));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [](int i) -> std::string {
    if (i == 0) return std::string(tr(STR_SUDOKU_DIFFICULTY_EASY));
    if (i == 1) return std::string(tr(STR_SUDOKU_DIFFICULTY_MEDIUM));
    return std::string(tr(STR_SUDOKU_DIFFICULTY_HARD));
  };
  auto rowSubtitle = [this](int i) -> std::string {
    char buf[96];
    if (cachedStats.bestTimeSec[i] > 0) {
      int rate = 0;
      if (cachedStats.startedCount[i] > 0) {
        rate = (cachedStats.completedCount[i] * 100) / cachedStats.startedCount[i];
      }
      snprintf(buf, sizeof(buf), "%s %02u:%02u · %u %s · %d%%", tr(STR_SUDOKU_BEST_TIME),
               static_cast<unsigned>(cachedStats.bestTimeSec[i] / 60),
               static_cast<unsigned>(cachedStats.bestTimeSec[i] % 60),
               static_cast<unsigned>(cachedStats.completedCount[i]), tr(STR_SUDOKU_COMPLETED), rate);
    } else {
      snprintf(buf, sizeof(buf), "%s", tr(STR_SUDOKU_NO_RECORD));
    }
    return std::string(buf);
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, 3, -1, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
