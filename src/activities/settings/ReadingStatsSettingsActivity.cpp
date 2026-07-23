#include "ReadingStatsSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdint>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {

enum class MenuItem : uint8_t { DailyGoal, Achievements, AchievementPopups, Count };

constexpr int MENU_ITEMS = static_cast<int>(MenuItem::Count);
constexpr StrId MENU_NAMES[MENU_ITEMS] = {
    StrId::STR_DAILY_GOAL,
    StrId::STR_ENABLE_ACHIEVEMENTS,
    StrId::STR_ACHIEVEMENT_POPUPS,
};

constexpr int DAILY_GOAL_ITEMS = CrossPointSettings::DAILY_GOAL_TARGET_COUNT;
constexpr StrId DAILY_GOAL_NAMES[DAILY_GOAL_ITEMS] = {
    StrId::STR_MIN_15,
    StrId::STR_MIN_30,
    StrId::STR_MIN_45,
    StrId::STR_MIN_60,
};

}  // namespace

void ReadingStatsSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void ReadingStatsSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, MENU_ITEMS, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, MENU_ITEMS);
    requestUpdate();
  });
}

void ReadingStatsSettingsActivity::handleSelection() {
  if (selectedIndex < 0 || selectedIndex >= MENU_ITEMS) return;

  switch (static_cast<MenuItem>(selectedIndex)) {
    case MenuItem::DailyGoal: {
      const uint8_t currentGoal = SETTINGS.dailyGoalTarget < DAILY_GOAL_ITEMS ? SETTINGS.dailyGoalTarget
                                                                              : CrossPointSettings::DAILY_GOAL_30_MIN;
      optionPopup.show(StrId::STR_DAILY_GOAL, DAILY_GOAL_NAMES, DAILY_GOAL_ITEMS, currentGoal, [this](int index) {
        SETTINGS.dailyGoalTarget = static_cast<uint8_t>(index);
        SETTINGS.saveToFile();
        requestUpdate();
      });
      requestUpdate();
      return;
    }
    case MenuItem::Achievements:
      SETTINGS.achievementsEnabled = !SETTINGS.achievementsEnabled;
      break;
    case MenuItem::AchievementPopups:
      SETTINGS.achievementPopups = !SETTINGS.achievementPopups;
      break;
    case MenuItem::Count:
      return;
  }

  SETTINGS.saveToFile();
  requestUpdate();
}

void ReadingStatsSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_READING_STATS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS, selectedIndex,
      [](int index) { return std::string(I18N.get(MENU_NAMES[index])); }, nullptr, nullptr,
      [](int index) -> std::string {
        switch (static_cast<MenuItem>(index)) {
          case MenuItem::DailyGoal: {
            const uint8_t goal = SETTINGS.dailyGoalTarget < DAILY_GOAL_ITEMS ? SETTINGS.dailyGoalTarget
                                                                             : CrossPointSettings::DAILY_GOAL_30_MIN;
            return I18N.get(DAILY_GOAL_NAMES[goal]);
          }
          case MenuItem::Achievements:
            return SETTINGS.achievementsEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case MenuItem::AchievementPopups:
            return SETTINGS.achievementPopups ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          case MenuItem::Count:
            return {};
        }
        return {};
      },
      true);

  const char* confirmLabel = selectedIndex == static_cast<int>(MenuItem::DailyGoal) ? tr(STR_SELECT) : tr(STR_TOGGLE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
