#include "ReadingStatsMenuActivity.h"

#include <I18n.h>

#include <memory>
#include <string>

#include "../../../CrossPointSettings.h"
#include "../../../components/UITheme.h"
#include "AchievementsActivity.h"
#include "AppMetricCard.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingProfileActivity.h"
#include "ReadingStatsActivity.h"

namespace {

// Screens listed in the Reading Stats sub-menu. Achievements is kept LAST so it
// can be hidden by simply trimming the active count when the setting is off.
enum class StatsScreen { Stats, Heatmap, Profile, Achievements };

struct MenuEntry {
  StrId titleId;
  StatsScreen screen;
  UIIcon icon;
};

constexpr MenuEntry kEntries[] = {
    {StrId::STR_READING_STATS, StatsScreen::Stats, UIIcon::ReadingStats},
    {StrId::STR_READING_HEATMAP, StatsScreen::Heatmap, UIIcon::ReadingHeatmap},
    {StrId::STR_READING_PROFILE, StatsScreen::Profile, UIIcon::ReadingProfile},
    {StrId::STR_ACHIEVEMENTS, StatsScreen::Achievements, UIIcon::Achievements},
};

constexpr int kEntryCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

// Achievements (the last row) is only listed when enabled in settings.
int activeEntryCount() { return SETTINGS.achievementsEnabled ? kEntryCount : kEntryCount - 1; }

}  // namespace

void ReadingStatsMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  requestUpdate();
}

void ReadingStatsMenuActivity::onExit() { Activity::onExit(); }

void ReadingStatsMenuActivity::openSelected() {
  if (selected < 0 || selected >= activeEntryCount()) {
    return;
  }
  // Leaves call finish() on Back, so pushing them returns control here.
  switch (kEntries[selected].screen) {
    case StatsScreen::Stats:
      startActivityForResultWith<ReadingStatsActivity>([](const ActivityResult&) {});
      break;
    case StatsScreen::Heatmap:
      startActivityForResultWith<ReadingHeatmapActivity>([](const ActivityResult&) {});
      break;
    case StatsScreen::Profile:
      startActivityForResultWith<ReadingProfileActivity>([](const ActivityResult&) {});
      break;
    case StatsScreen::Achievements:
      startActivityForResultWith<AchievementsActivity>([](const ActivityResult&) {});
      break;
  }
}

void ReadingStatsMenuActivity::loop() {
  const int count = activeEntryCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (handleListTouch(selected, count, listTop, listHeight, false) == ListTouchResult::Activated) {
    openSelected();
    return;
  }

  buttonNavigator.onNext([this, count] {
    selected = ButtonNavigator::nextIndex(selected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selected = ButtonNavigator::previousIndex(selected, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Per the apps convention, a sub-app's Back returns to the Apps menu.
    activityManager.goToApps();
  }
}

void ReadingStatsMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_READING_STATS));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawButtonMenu(
      renderer, Rect{0, listY, sw, listH}, activeEntryCount(), selected,
      [](int i) { return std::string(I18n::getInstance().get(kEntries[i].titleId)); },
      [](int i) { return AppMetricCard::menuIcon(kEntries[i].icon); });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
