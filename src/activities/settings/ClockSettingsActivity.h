#pragma once
#include "activities/UiListActivity.h"

// System date/time configuration, with an optional external RTC for persistence.
class ClockSettingsActivity final : public UiListActivity {
 public:
  explicit ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 7;

  void onEnter() override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  bool handleCustomInput() override;

  // Member buffers keep formatted row values alive across the frame.
  char dateTime_[24] = {0};
  char syncTime_[9] = {0};
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
  bool waitForBackRelease_ = false;
  bool waitForConfirmRelease_ = false;
};
