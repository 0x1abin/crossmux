#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class DateTimeSettingsActivity final : public Activity {
 public:
  explicit DateTimeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DateTimeSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t {
    Menu,
    ManualEdit,
  };

  enum class MenuItem : uint8_t {
    AutoTime,
    DateTime,
    TimeZone,
    Hour24,
    SyncNow,
    Count,
  };

  enum class EditField : uint8_t {
    Year,
    Month,
    Day,
    Hour,
    Minute,
    Count,
  };

  static constexpr int MENU_ITEM_COUNT = static_cast<int>(MenuItem::Count);
  static constexpr int EDIT_FIELD_COUNT = static_cast<int>(EditField::Count);

  ButtonNavigator buttonNavigator;
  Mode mode = Mode::Menu;
  int selectedMenuItem = 0;
  int selectedEditField = 0;
  int year = 2024;
  unsigned month = 1;
  unsigned day = 1;
  unsigned hour = 0;
  unsigned minute = 0;

  void loopMenu();
  void loopManualEdit();
  void renderMenu();
  void renderManualEdit();
  void activateMenuItem();
  void beginManualEdit();
  void adjustEditField(int delta);
  bool applyManualTime();
};
