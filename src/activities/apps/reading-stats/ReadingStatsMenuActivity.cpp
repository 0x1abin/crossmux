#include "ReadingStatsMenuActivity.h"

#include <I18n.h>

#include <memory>
#include <string>

#include "AchievementsActivity.h"
#include "AppMetricCard.h"
#include "CrossPointSettings.h"
#include "ReadingHeatmapActivity.h"
#include "ReadingProfileActivity.h"
#include "ReadingStatsActivity.h"
#include "components/UITheme.h"

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
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Hit-test with the theme's own drawButtonMenu geometry so a tap lands on
  // the drawn rows under every theme: Lyra draws from rect.y without the
  // vertical offset, and RoundedRaff / Inx derive the row height from the font
  // and page the rows. The geometry's pageStart maps the visible row back to
  // the true index.
  const auto geo = GUI.getMenuRowGeometry(renderer, Rect{0, listTop, sw, listH}, selected, count);
  int touched = -1;
  const auto touch =
      mappedInput.rowTouch(touched, geo.firstRowY, geo.rowStep, geo.pageCount, geo.xStart, geo.xEnd, geo.rowHeight);
  if (touch == MappedInputManager::RowTouch::Down || touch == MappedInputManager::RowTouch::Tap) {
    touched += geo.pageStart;
  }
  if (touch == MappedInputManager::RowTouch::Down) {
    if (selected != touched) {
      selected = touched;
      requestUpdate();
    }
  } else if (touch == MappedInputManager::RowTouch::Tap) {
    selected = touched;
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
