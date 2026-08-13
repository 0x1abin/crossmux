#pragma once

#include <I18n.h>

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

// Apps menu — the single entry-point on the home screen for all non-reader sub-apps
// (Sudoku, Gomoku, Ugly Avatar, ...). The full list is the constexpr `kAppEntries` table
// in AppsMenuActivity.cpp; add a new app by assigning a stable AppId, appending one row,
// and adding goTo<App>() in ActivityManager. See src/activities/apps/README.md.
class AppsMenuActivity final : public Activity {
 public:
  AppsMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AppsMenu", renderer, mappedInput) {}
  ~AppsMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return MainTab::Apps; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;

  static int getAppCount();
  static StrId getAppTitleId(int appIndex);
  static bool isAppVisible(int appIndex);
  static bool setAppVisible(int appIndex, bool visible);

 private:
  enum class SelectionMode { Hidden, Touch, Buttons };

  static int getVisibleAppCount();
  static int getAppIndexForVisibleIndex(int visibleIndex);
  bool usesIconLayout() const;
  int iconIndexFromPoint(int x, int y) const;
  void openSelected();
  void drawIconGrid(const Rect& rect, int visibleCount, bool showSelection) const;

  ButtonNavigator buttonNavigator;
  SelectionMode selectionMode = SelectionMode::Buttons;
  int selected = 0;
};
