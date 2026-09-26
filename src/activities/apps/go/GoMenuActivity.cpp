#include "GoMenuActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "GoGameActivity.h"
#include "activities/apps/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

const char* difficultyOptionLabel(GoDifficulty d) {
  switch (d) {
    case GoDifficulty::Kyu20: return tr(STR_GO_DIFF_20K);
    case GoDifficulty::Kyu18: return tr(STR_GO_DIFF_18K);
    case GoDifficulty::Kyu15: return tr(STR_GO_DIFF_15K);
    case GoDifficulty::Kyu12: return tr(STR_GO_DIFF_12K);
    case GoDifficulty::Kyu10: return tr(STR_GO_DIFF_10K);
    case GoDifficulty::Kyu7: return tr(STR_GO_DIFF_7K);
    case GoDifficulty::Kyu5: return tr(STR_GO_DIFF_5K);
    default: return "";
  }
}

}  // namespace

GoMenuActivity::GoMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GoMenu", renderer, mappedInput) {}

void GoMenuActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  buildItems();
  selected = 0;
  showingStats = false;
  cachedStats = GoStore::loadStats();
  requestUpdate();
}

void GoMenuActivity::onExit() { Activity::onExit(); }

void GoMenuActivity::buildItems() {
  items.clear();
  items.reserve(5);
  hasResume = false;

  if (GoStore::hasInProgress()) {
    GoSaveSlot slot;
    if (GoStore::load(slot)) {
      hasResume = true;
      resumeMode = slot.mode;
      resumeDifficulty = slot.difficulty;
      resumeMoveCount = 0;  // move log is not persisted; show time only
      resumeElapsedSec = slot.elapsedSec;
      items.push_back({ItemKind::Continue, false});
    } else {
      LOG_ERR("GO", "Resume save unreadable; clearing");
      GoStore::clear();
    }
  }

  items.push_back({ItemKind::NewAi, false});
  items.push_back({ItemKind::NewTwoPlayer, false});
  items.push_back({ItemKind::Stats, false});
}

void GoMenuActivity::loop() {
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

void GoMenuActivity::onSelect() {
  if (selected < 0 || selected >= static_cast<int>(items.size())) return;
  const Item& it = items[selected];
  if (it.disabled) return;
  switch (it.kind) {
    case ItemKind::Continue:
      activityManager.replaceActivityWith<GoGameActivity>(resumeMode, resumeDifficulty, true);
      return;
    case ItemKind::NewAi: {
      const char* options[static_cast<int>(GoDifficulty::Count)] = {};
      for (int i = 0; i < static_cast<int>(GoDifficulty::Count); i++) {
        options[i] = difficultyOptionLabel(static_cast<GoDifficulty>(i));
      }
      difficultyPopup.show(tr(STR_GO_DIFFICULTY), options, static_cast<int>(GoDifficulty::Count),
                           static_cast<int>(GoDifficulty::Kyu12), [this](const int index) {
                             GoStore::clear();
                             activityManager.replaceActivityWith<GoGameActivity>(
                                 GoMode::VsAi, static_cast<GoDifficulty>(index), false);
                           });
      requestUpdate();
      return;
    }
    case ItemKind::NewTwoPlayer:
      GoStore::clear();
      activityManager.replaceActivityWith<GoGameActivity>(GoMode::TwoPlayer, GoDifficulty::Kyu12, false);
      return;
    case ItemKind::Stats:
      showingStats = true;
      requestUpdate();
      return;
  }
}

void GoMenuActivity::render(RenderLock&&) {
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

void GoMenuActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GO_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [this](int i) -> std::string {
    if (i < 0 || i >= static_cast<int>(items.size())) return "";
    const Item& it = items[i];
    switch (it.kind) {
      case ItemKind::Continue:
        return std::string(tr(STR_GAME_CONTINUE));
      case ItemKind::NewAi:
        return std::string(tr(STR_GO_NEW_AI));
      case ItemKind::NewTwoPlayer:
        return std::string(tr(STR_GO_NEW_2P));
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
        const char* modeLabel = (resumeMode == GoMode::VsAi) ? tr(STR_GO_MODE_AI) : tr(STR_GO_MODE_2P);
        snprintf(buf, sizeof(buf), "%s · %02u:%02u", modeLabel, static_cast<unsigned>(resumeElapsedSec / 60),
                 static_cast<unsigned>(resumeElapsedSec % 60));
        return std::string(buf);
      }
      case ItemKind::NewAi:
        return std::string(tr(STR_GO_DESC_AI_PICK));
      case ItemKind::NewTwoPlayer:
        return std::string(tr(STR_GO_DESC_2P));
      case ItemKind::Stats:
        return std::string(tr(STR_GAME_STATS_DESC));
    }
    return "";
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, static_cast<int>(items.size()), selected, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void GoMenuActivity::renderStats() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_GAME_STATS));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  auto rowTitle = [](int i) -> std::string {
    (void)i;
    return std::string("9×9");
  };
  auto rowSubtitle = [this](int i) -> std::string {
    (void)i;
    char buf[96];
    if (cachedStats.startedCount > 0) {
      snprintf(buf, sizeof(buf), "%s %u · %s %u · %s %u", tr(STR_GO_BLACK), static_cast<unsigned>(cachedStats.blackWins),
               tr(STR_GO_WHITE), static_cast<unsigned>(cachedStats.whiteWins), tr(STR_GO_PLAYED),
               static_cast<unsigned>(cachedStats.startedCount));
    } else {
      snprintf(buf, sizeof(buf), "%s", tr(STR_GAME_NO_RECORD));
    }
    return std::string(buf);
  };

  GUI.drawList(renderer, Rect{0, listY, sw, listH}, 1, -1, rowTitle, rowSubtitle);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
