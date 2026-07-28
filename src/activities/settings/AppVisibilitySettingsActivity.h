#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class AppVisibilitySettingsActivity final : public Activity {
 public:
  explicit AppVisibilitySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppVisibilitySettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void toggleSelected();

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool dirty = false;
};
