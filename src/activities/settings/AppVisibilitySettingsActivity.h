#pragma once

#include <vector>

#include "activities/UiListActivity.h"

class AppVisibilitySettingsActivity final : public UiListActivity {
 public:
  explicit AppVisibilitySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  std::vector<freeink::ui::ListItem> rowItems;
  bool waitForConfirmRelease = false;
  bool dirty = false;
};
