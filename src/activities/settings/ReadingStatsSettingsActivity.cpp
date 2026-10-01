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
  UiListActivity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void ReadingStatsSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  UiListActivity::loop();
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

void ReadingStatsSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

int ReadingStatsSettingsActivity::listCount() const { return MENU_ITEMS; }
const char* ReadingStatsSettingsActivity::headerTitle() const { return tr(STR_READING_STATS); }
void ReadingStatsSettingsActivity::activateIndex(int index) {
  selectedIndex = index;
  app.clearTapFlash();
  handleSelection();
}
void ReadingStatsSettingsActivity::buildScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(metrics.verticalSpacing);
  static fui::ListProps props;
  props = {};
  props.count = MENU_ITEMS;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = SETTINGS.uiTheme == CrossPointSettings::INX ? screen.theme().bodyText : screen.theme().smallText;
  props.labelText.maxLines = 2;
  if (SETTINGS.uiTheme == CrossPointSettings::INX) {
    props.rowHeight = GUI.getListRowStep(false);
    props.scrollIndicator = props.count > screen.contentRect().height / props.rowHeight;
    props.labelText.maxLines = 1;
    props.valueInset = 0;
    props.valueText = screen.theme().bodyText;
  }
  props.rowProvider = [](void*, uint16_t index, fui::ListItem& item) {
    item.label = I18N.get(MENU_NAMES[index]);
    switch (static_cast<MenuItem>(index)) {
      case MenuItem::DailyGoal: {
        const uint8_t goal = SETTINGS.dailyGoalTarget < DAILY_GOAL_ITEMS ? SETTINGS.dailyGoalTarget
                                                                         : CrossPointSettings::DAILY_GOAL_30_MIN;
        item.value = I18N.get(DAILY_GOAL_NAMES[goal]);
        break;
      }
      case MenuItem::Achievements:
        GUI.setCheckboxRow(item, SETTINGS.achievementsEnabled);
        break;
      case MenuItem::AchievementPopups:
        GUI.setCheckboxRow(item, SETTINGS.achievementPopups);
        break;
      case MenuItem::Count:
        break;
    }
  };
  syncListViewport(screen, props);
  if (SETTINGS.uiTheme == CrossPointSettings::INX) {
    // Preserve the legacy full-width INX rows; only the boolean control changes.
    auto rect = screen.contentRect();
    rect.x = 0;
    rect.width = renderer.getScreenWidth();
    fui::list(screen.frame(), rect, screen.resolveListProps(props));
  } else {
    screen.list(props);
  }
}
