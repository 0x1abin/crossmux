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

  static int getAppCount();
  static StrId getAppTitleId(int appIndex);
  static bool isAppVisible(int appIndex);
  static bool setAppVisible(int appIndex, bool visible);

 private:
  static int getVisibleAppCount();
  static int getAppIndexForVisibleIndex(int visibleIndex);

  // Geometry of the paginated app list. Shared by render() and touch
  // hit-testing so the drawn rows and the tappable rows cannot drift apart.
  struct MenuLayout {
    int listY;
    int listH;
    int spacing;
    int rowStep;
    int perPage;
    int page;
    int pageStart;
    int pageCount;
  };
  MenuLayout menuLayout(int visibleCount) const;
  void activateSelected();

  ButtonNavigator buttonNavigator;
  int selected = 0;
};
