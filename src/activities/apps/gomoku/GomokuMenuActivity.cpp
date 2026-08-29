#include "GomokuMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "GomokuGameActivity.h"

GomokuMenuActivity::GomokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GomokuMenu", renderer, mappedInput) {}

void GomokuMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  buildItems();
  selected = 0;
  showingStats = false;
  cachedStats = GomokuStore::loadStats();
  requestUpdate();
}

void GomokuMenuActivity::onExit() { Activity::onExit(); }

void GomokuMenuActivity::buildItems() {
  items.clear();
  items.reserve(6);
  hasResume = false;

  if (GomokuStore::hasInProgress()) {
    GomokuSaveSlot slot;
    if (GomokuStore::load(slot)) {
      hasResume = true;
      resumeMode = slot.mode;
      resumeAiLevel = slot.aiLevel;
      resumeBoardSize = slot.board.boardSize;
      resumeMoveCount = slot.board.moveCount;
      resumeElapsedSec = slot.elapsedSec;
      Item it;
      it.kind = ItemKind::Continue;
      it.mode = slot.mode;
      it.boardSize = slot.board.boardSize;
      items.push_back(it);
    } else {
      // File exists but didn't decode (truncation, version mismatch, etc.).
      // Drop it so we don't keep retrying on every menu entry.
      LOG_ERR("GMK", "Resume save unreadable; clearing");
      GomokuStore::clear();
    }
  }

  // 2-Player options (always enabled in this release).
  items.push_back({ItemKind::NewGame, GomokuMode::TwoPlayer, 15, false});
  items.push_back({ItemKind::NewGame, GomokuMode::TwoPlayer, 9, false});
  // AI options: tapping these opens a difficulty modal before launching.
  items.push_back({ItemKind::NewGame, GomokuMode::VsAi, 15, false});
  items.push_back({ItemKind::NewGame, GomokuMode::VsAi, 9, false});
  items.push_back({ItemKind::Stats, GomokuMode::TwoPlayer, 15, false});
}

void GomokuMenuActivity::loop() {
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

void GomokuMenuActivity::onSelect() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  const Item& it = items[selected];
  if (it.disabled) return;  // legacy guard; no items currently use it
  switch (it.kind) {
    case ItemKind::Continue:
      activityManager.replaceActivityWith<GomokuGameActivity>(it.mode, it.boardSize, true, resumeAiLevel);
      return;
    case ItemKind::NewGame:
      if (it.mode == GomokuMode::VsAi) {
        pendingAiBoardSize = it.boardSize;
        const char* options[] = {tr(STR_GOMOKU_DIFF_EASY), tr(STR_GOMOKU_DIFF_MEDIUM), tr(STR_GOMOKU_DIFF_HARD)};
        difficultyPopup.show(tr(STR_GOMOKU_DIFFICULTY), options, 3, static_cast<int>(GomokuAiLevel::Medium),
                             [this](const int index) {
                               GomokuStore::clear();
                               activityManager.replaceActivityWith<GomokuGameActivity>(
                                   GomokuMode::VsAi, pendingAiBoardSize, false, static_cast<GomokuAiLevel>(index));
                             });
        requestUpdate();
        return;
      }
      GomokuStore::clear();
      activityManager.replaceActivityWith<GomokuGameActivity>(it.mode, it.boardSize, false);
      return;
    case ItemKind::Stats:
      showingStats = true;
      requestUpdate();
      return;
  }
}

void GomokuMenuActivity::render(RenderLock&&) {
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

void GomokuMenuActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GOMOKU_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    switch (it.kind) {
      case ItemKind::Continue:
        return std::string(tr(STR_GAME_CONTINUE));
      case ItemKind::NewGame:
        if (it.mode == GomokuMode::TwoPlayer && it.boardSize == 15) return std::string(tr(STR_GOMOKU_NEW_2P_15));
        if (it.mode == GomokuMode::TwoPlayer && it.boardSize == 9) return std::string(tr(STR_GOMOKU_NEW_2P_9));
        if (it.mode == GomokuMode::VsAi && it.boardSize == 15) return std::string(tr(STR_GOMOKU_NEW_AI_15));
        if (it.mode == GomokuMode::VsAi && it.boardSize == 9) return std::string(tr(STR_GOMOKU_NEW_AI_9));
        return "";
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
        char buf[80];
        const char* modeLabel = (resumeMode == GomokuMode::VsAi) ? tr(STR_GOMOKU_MODE_AI) : tr(STR_GOMOKU_MODE_2P);
        const char* sizeLabel = (resumeBoardSize == 9) ? tr(STR_GOMOKU_BOARD_9) : tr(STR_GOMOKU_BOARD_15);
        if (resumeMode == GomokuMode::VsAi) {
          const char* lvl = (resumeAiLevel == GomokuAiLevel::Easy)   ? tr(STR_GOMOKU_DIFF_EASY)
                            : (resumeAiLevel == GomokuAiLevel::Hard) ? tr(STR_GOMOKU_DIFF_HARD)
                                                                     : tr(STR_GOMOKU_DIFF_MEDIUM);
          snprintf(buf, sizeof(buf), "%s · %s · %s · %02u:%02u · %u", modeLabel, lvl, sizeLabel,
                   static_cast<unsigned>(resumeElapsedSec / 60), static_cast<unsigned>(resumeElapsedSec % 60),
                   static_cast<unsigned>(resumeMoveCount));
        } else {
          snprintf(buf, sizeof(buf), "%s · %s · %02u:%02u · %u", modeLabel, sizeLabel,
                   static_cast<unsigned>(resumeElapsedSec / 60), static_cast<unsigned>(resumeElapsedSec % 60),
                   static_cast<unsigned>(resumeMoveCount));
        }
        return std::string(buf);
      }
      case ItemKind::NewGame:
        if (it.mode == GomokuMode::VsAi) return std::string(tr(STR_GOMOKU_DESC_AI_PICK));
        if (it.boardSize == 9) return std::string(tr(STR_GOMOKU_DESC_2P_9));
        return std::string(tr(STR_GOMOKU_DESC_2P_15));
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

void GomokuMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GAME_STATS));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [](int i) -> std::string {
    if (i == 0) return std::string(tr(STR_GOMOKU_BOARD_15));
    return std::string(tr(STR_GOMOKU_BOARD_9));
  };
  auto rowSubtitle = [this](int i) -> std::string {
    char buf[96];
    const uint8_t s = (i == 0) ? 0 : 1;
    if (cachedStats.bestTimeSec[s] > 0 || cachedStats.startedCount[s] > 0) {
      snprintf(buf, sizeof(buf), "%s %02u:%02u · %s %u · %s %u · %s %u", tr(STR_GAME_BEST_TIME),
               static_cast<unsigned>(cachedStats.bestTimeSec[s] / 60),
               static_cast<unsigned>(cachedStats.bestTimeSec[s] % 60), tr(STR_GOMOKU_BLACK),
               static_cast<unsigned>(cachedStats.blackWins[s]), tr(STR_GOMOKU_WHITE),
               static_cast<unsigned>(cachedStats.whiteWins[s]), tr(STR_GOMOKU_DRAW),
               static_cast<unsigned>(cachedStats.draws[s]));
    } else {
      snprintf(buf, sizeof(buf), "%s", tr(STR_GAME_NO_RECORD));
    }
    return std::string(buf);
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, 2, -1, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
