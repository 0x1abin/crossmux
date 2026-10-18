#pragma once

#include <array>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/UiContentSelection.h"
#include "util/ButtonNavigator.h"

struct ReadingBookStats;

class ReadingStatsActivity final : public Activity, public UiAppHost {
  static constexpr freeink::ui::ActionId ACTION_ENTRY = 1;
  struct TouchTarget {
    Rect bounds;
    int entry;
  };
  std::array<TouchTarget, 4> touchTargets{};
  int touchTargetCount = 0;
  ButtonNavigator buttonNavigator;
  std::atomic<int> selectedIndex{0};
  std::atomic<int> viewIndex{0};
  UiContentSelection contentSelection;
  bool contentSelectionVisibleForFrame = false;
  bool waitForConfirmRelease = false;
  bool waitForBackRelease = false;
  bool mainTabEnabled = false;
  std::atomic<bool> waitingForCoverRender{false};
  std::atomic<bool> renderedCoverMissing{false};
  std::atomic<int> renderedCoverView{-1};
  int attemptedCoverView = -1;
  void openSelectedEntry();
  void confirmRemoveSelectedBook();
  void guardBackReturn();
  void prepareVisibleCover();
  bool usesInxLayout() const;
  void renderInx(int view);
  int displayedIndex() const;
  void establishContentSelection(int index);
  void moveSelectionTo(int index);
  bool consumeSelectionEntryInput();
  enum class InputFlow { Stop, TouchOnly, All };
  InputFlow handleButtons();

 public:
  explicit ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool mainTabEnabled = false)
      : Activity("ReadingStats", renderer, mappedInput), UiAppHost(renderer), mainTabEnabled(mainTabEnabled) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return mainTabEnabled ? MainTab::Statistics : MainTab::None; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
  bool hasContentSelection() const override;
};
