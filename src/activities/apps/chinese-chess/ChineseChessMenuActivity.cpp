#include "ChineseChessMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "ChineseChessGameActivity.h"

ChineseChessMenuActivity::ChineseChessMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ChineseChessMenu", renderer, mappedInput) {}

void ChineseChessMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  buildItems();
  selected = 0;
  showingStats = false;
  cachedStats = ChineseChessStore::loadStats();
  requestUpdate();
}

void ChineseChessMenuActivity::onExit() { Activity::onExit(); }

void ChineseChessMenuActivity::buildItems() {
  items.clear();
  items.reserve(4);
  hasResume = false;

  if (ChineseChessStore::hasInProgress()) {
    ChineseChessSaveSlot slot;
    if (ChineseChessStore::load(slot)) {
      hasResume = true;
      resumeMode = slot.mode;
      resumeAiLevel = slot.aiLevel;
      resumeMoveCount = slot.board.moveCount;
      resumeElapsedSec = static_cast<uint16_t>(slot.redElapsedSec + slot.blackElapsedSec);
      Item it;
      it.kind = ItemKind::Continue;
      it.mode = slot.mode;
      items.push_back(it);
    } else {
      LOG_ERR("XQI", "Resume save unreadable; clearing");
      ChineseChessStore::clear();
    }
  }

  items.push_back({ItemKind::NewGame, ChineseChessMode::TwoPlayer, false});
  items.push_back({ItemKind::NewGame, ChineseChessMode::VsAi, false});
  items.push_back({ItemKind::Stats, ChineseChessMode::TwoPlayer, false});
}

void ChineseChessMenuActivity::loop() {
  if (showingStats) {
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      showingStats = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      showingStats = false;
      requestUpdate();
    }
    return;
  }

  if (difficultyPopup.isActive()) {
    difficultyPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  const int n = static_cast<int>(items.size());
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (handleListTouch(selected, n, listTop, listHeight, true) == ListTouchResult::Activated) {
    onSelect();
    return;
  }

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
    activityManager.goToApps();
  }
}

void ChineseChessMenuActivity::onSelect() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  const Item& it = items[selected];
  if (it.disabled) return;
  switch (it.kind) {
    case ItemKind::Continue:
      activityManager.replaceActivityWith<ChineseChessGameActivity>(it.mode, true, resumeAiLevel);
      return;
    case ItemKind::NewGame:
      if (it.mode == ChineseChessMode::VsAi) {
        const char* options[] = {tr(STR_GOMOKU_DIFF_EASY), tr(STR_GOMOKU_DIFF_MEDIUM), tr(STR_GOMOKU_DIFF_HARD)};
        difficultyPopup.show(tr(STR_GOMOKU_DIFFICULTY), options, 3, static_cast<int>(ChineseChessAiLevel::Medium),
                             [this](const int index) {
                               ChineseChessStore::clear();
                               activityManager.replaceActivityWith<ChineseChessGameActivity>(
                                   ChineseChessMode::VsAi, false, static_cast<ChineseChessAiLevel>(index));
                             });
        requestUpdate();
        return;
      }
      ChineseChessStore::clear();
      activityManager.replaceActivityWith<ChineseChessGameActivity>(it.mode, false);
      return;
    case ItemKind::Stats:
      showingStats = true;
      requestUpdate();
      return;
  }
}

void ChineseChessMenuActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  if (showingStats) {
    renderStats();
  } else {
    renderList();
    if (difficultyPopup.processRender(renderer, mappedInput)) return;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ChineseChessMenuActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_CHINESE_CHESS_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    switch (it.kind) {
      case ItemKind::Continue:
        return std::string(tr(STR_GAME_CONTINUE));
      case ItemKind::NewGame:
        if (it.mode == ChineseChessMode::TwoPlayer) return std::string(tr(STR_CHINESE_CHESS_NEW_2P));
        return std::string(tr(STR_CHINESE_CHESS_NEW_AI));
      case ItemKind::Stats:
        return std::string(tr(STR_GAME_STATS));
    }
    return "";
  };

  auto rowSubtitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    switch (it.kind) {
      case ItemKind::Continue: {
        char buf[96];
        const char* modeLabel =
            (resumeMode == ChineseChessMode::VsAi) ? tr(STR_CHINESE_CHESS_MODE_AI) : tr(STR_CHINESE_CHESS_MODE_2P);
        if (resumeMode == ChineseChessMode::VsAi) {
          const char* lvl = (resumeAiLevel == ChineseChessAiLevel::Easy)   ? tr(STR_GOMOKU_DIFF_EASY)
                            : (resumeAiLevel == ChineseChessAiLevel::Hard) ? tr(STR_GOMOKU_DIFF_HARD)
                                                                           : tr(STR_GOMOKU_DIFF_MEDIUM);
          snprintf(buf, sizeof(buf), "%s · %s · %02u:%02u · %u", modeLabel, lvl,
                   static_cast<unsigned>(resumeElapsedSec / 60), static_cast<unsigned>(resumeElapsedSec % 60),
                   static_cast<unsigned>(resumeMoveCount));
        } else {
          snprintf(buf, sizeof(buf), "%s · %02u:%02u · %u", modeLabel, static_cast<unsigned>(resumeElapsedSec / 60),
                   static_cast<unsigned>(resumeElapsedSec % 60), static_cast<unsigned>(resumeMoveCount));
        }
        return std::string(buf);
      }
      case ItemKind::NewGame:
        if (it.mode == ChineseChessMode::VsAi) return std::string(tr(STR_CHINESE_CHESS_DESC_AI_PICK));
        return std::string(tr(STR_CHINESE_CHESS_DESC_2P));
      case ItemKind::Stats:
        return std::string(tr(STR_GAME_STATS_DESC));
    }
    return "";
  };

  auto rowDimmed = [this](int i) -> bool {
    if (i < 0 || i >= static_cast<int>(items.size())) return false;
    return items[i].disabled;
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, static_cast<int>(items.size()), selected, rowTitle, rowSubtitle,
               nullptr, nullptr, false, rowDimmed);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ChineseChessMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GAME_STATS));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [](int /*i*/) -> std::string { return std::string(tr(STR_CHINESE_CHESS_TITLE)); };
  auto rowSubtitle = [this](int /*i*/) -> std::string {
    char buf[112];
    if (cachedStats.bestTimeSec > 0 || cachedStats.startedCount > 0) {
      snprintf(buf, sizeof(buf), "%s %02u:%02u · %s %u · %s %u · %s %u", tr(STR_GAME_BEST_TIME),
               static_cast<unsigned>(cachedStats.bestTimeSec / 60), static_cast<unsigned>(cachedStats.bestTimeSec % 60),
               tr(STR_CHINESE_CHESS_RED), static_cast<unsigned>(cachedStats.redWins), tr(STR_CHINESE_CHESS_BLACK),
               static_cast<unsigned>(cachedStats.blackWins), tr(STR_CHINESE_CHESS_DRAW),
               static_cast<unsigned>(cachedStats.draws));
    } else {
      snprintf(buf, sizeof(buf), "%s", tr(STR_GAME_NO_RECORD));
    }
    return std::string(buf);
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, 1, -1, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
