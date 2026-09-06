#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"

class BluetoothSettingsActivity final : public UiListActivity {
 public:
  BluetoothSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BluetoothSettings", renderer, mappedInput, true) {}

  void onEnter() override;
  void onExit() override;
  bool keepsBluetoothAlive() const override { return true; }

 private:
  enum class View : uint8_t { Menu, Scan, Paired };
  enum class Action : uint8_t { Toggle, Scan, Paired, Map, Disconnect };
  static constexpr int kMaxRows = 24;

  View view = View::Menu;
  freeink::ui::ListItem rowItems[kMaxRows]{};
  int16_t rowValues[kMaxRows]{};
  int rowCount = 0;
  char banner[64]{};
  unsigned long bannerUntil = 0;
  bool awaitingConnect = false;
  bool pairedHoldActionTaken = false;
  uint8_t renderedDeviceCount = 0xFF;
  bool renderedScanning = false;

  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override { return tr(STR_BLUETOOTH); }
  void drawChrome() override;

  void rebuildRows();
  bool ensureAvailable();
  void enterScanView();
  void connectTo(const char* address);
  void forgetPaired(int index);
  void setBanner(const char* text);
};
