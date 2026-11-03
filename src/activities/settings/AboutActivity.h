#pragma once
#include <string>

#include "activities/UiListActivity.h"

// Read-only device information: detected hardware (device profile, display
// controller, touch, frontlight, RTC, IMU), firmware version, and MAC address.
// Rows are informational; activating one does nothing.
class AboutActivity final : public UiListActivity {
 public:
  explicit AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 18;

  void onEnter() override;

 private:
  int listCount() const override { return rowCount_; }
  void buildScreen(UiScreen& screen) override;
  Rect contentRect() const;
  void activateIndex(int) override {}
  const char* headerTitle() const override;

  int rowCount_ = 0;
  std::string rowValues_[ITEM_COUNT];
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
