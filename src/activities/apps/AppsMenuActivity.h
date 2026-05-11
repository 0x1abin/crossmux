#pragma once

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

// Apps menu — the single entry-point on the home screen for all non-reader sub-apps
// (Sudoku, Gomoku, Ugly Avatar, ...). The full list is the constexpr `kAppEntries` table
// in AppsMenuActivity.cpp; add a new app by appending one row there and a goTo<App>() in
// ActivityManager. See src/activities/apps/README.md for the full convention.
class AppsMenuActivity final : public Activity {
 public:
  AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  ~AppsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selected = 0;
};
