#pragma once

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"

// Sub-menu hub for the Reading Analytics suite. Reached from the Apps menu via
// ActivityManager::goToReadingStatsMenu(); lists the suite's screens and pushes
// the selected leaf (startActivityForResult) so Back returns here. Additional
// screens (Profile, Achievements) are appended to kEntries as their phases land.
class ReadingStatsMenuActivity final : public Activity {
 public:
  ReadingStatsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsMenu", renderer, mappedInput) {}
  ~ReadingStatsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void openSelected();
  ButtonNavigator buttonNavigator;
  int selected = 0;
};
