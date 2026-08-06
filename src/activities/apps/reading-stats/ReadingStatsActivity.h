#pragma once

#include "../../Activity.h"
#include "util/ButtonNavigator.h"

struct ReadingBookStats;

class ReadingStatsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  bool mainTabEnabled = false;
  bool waitingForCoverRender = false;
  bool renderedCoverMissing = false;
  int renderedCoverView = -1;
  int attemptedCoverView = -1;
  void openSelectedEntry();
  void confirmRemoveSelectedBook();
  void guardBackReturn();
  void prepareVisibleCover();
  bool usesInxLayout() const;
  void renderInx();

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool mainTabEnabled = false)
      : Activity("ReadingStats", renderer, mappedInput), mainTabEnabled(mainTabEnabled) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return mainTabEnabled ? MainTab::Statistics : MainTab::None; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
