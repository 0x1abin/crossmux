#pragma once

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class ReadingStatsSettingsActivity final : public UiListActivity {
 public:
  explicit ReadingStatsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReadingStatsSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;
  int selectedIndex = 0;

  void handleSelection();
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
