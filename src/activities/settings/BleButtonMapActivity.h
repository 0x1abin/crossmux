#pragma once

#include <I18n.h>

#include "BleKeyMapping.h"
#include "activities/UiListActivity.h"

class BleButtonMapActivity final : public UiListActivity {
 public:
  BleButtonMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BleButtonMap", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool keepsBluetoothAlive() const override { return true; }

 private:
  struct Function {
    bleinput::Action action;
    StrId label;
  };
  static constexpr uint8_t kFunctionCount = static_cast<uint8_t>(bleinput::Action::Count);
  static const Function kFunctions[kFunctionCount];

  int captureTarget = -1;
  freeink::ui::ListItem rowItems[kFunctionCount]{};
  char valueBuffers[kFunctionCount][32]{};
  const char* unavailable = nullptr;

  int listCount() const override { return captureTarget >= 0 ? 0 : kFunctionCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;
  const char* headerTitle() const override { return tr(STR_BT_MAP_BUTTONS); }
  void drawChrome() override;
};
