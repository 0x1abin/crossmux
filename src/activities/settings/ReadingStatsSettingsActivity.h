#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class ReadingStatsSettingsActivity final : public Activity {
 public:
  explicit ReadingStatsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingStatsSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;

  void handleSelection();
};
