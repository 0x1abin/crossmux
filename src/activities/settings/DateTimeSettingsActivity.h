#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Manual local date/time editor, opened from ClockSettingsActivity.
class DateTimeSettingsActivity final : public Activity, private UiAppHost {
 public:
  explicit DateTimeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DateTimeSettings", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class EditField : uint8_t {
    Year,
    Month,
    Day,
    Hour,
    Minute,
    Count,
  };

  static constexpr int EDIT_FIELD_COUNT = static_cast<int>(EditField::Count);

  ButtonNavigator buttonNavigator;
  bool waitForConfirmRelease_ = false;
  int selectedEditField = 0;
  int year = 2024;
  unsigned month = 1;
  unsigned day = 1;
  unsigned hour = 0;
  unsigned minute = 0;

  void beginManualEdit();
  void adjustEditField(int delta);
  bool applyManualTime();
  void cancelManualEdit();
  void confirmManualEdit();

  static constexpr freeink::ui::ActionId ACTION_STEP = 1;
  static constexpr freeink::ui::ActionId ACTION_CANCEL = 2;
  static constexpr freeink::ui::ActionId ACTION_OK = 3;
  static void manualScreen(UiScreen& screen, void* user);
  static void onStep(const freeink::ui::ActionEvent& event, void* user);
  static void onCancel(const freeink::ui::ActionEvent& event, void* user);
  static void onOk(const freeink::ui::ActionEvent& event, void* user);
  void buildManualScreen(UiScreen& screen);
};
