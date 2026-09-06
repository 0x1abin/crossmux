#include "BluetoothSettingsActivity.h"

#include <BleKeyboardHost.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

#if FREEINK_CAP_BLE_HID_HOST

namespace fui = freeink::ui;

namespace {
constexpr unsigned long kBannerMs = 2500;
constexpr unsigned long kForgetHoldMs = 1200;
constexpr uint32_t kScanMs = 8000;
}  // namespace

void BluetoothSettingsActivity::onEnter() {
  view = View::Menu;
  UiListActivity::onEnter();
  if (SETTINGS.bluetoothEnabled) ensureAvailable();
}

void BluetoothSettingsActivity::onExit() {
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

void BluetoothSettingsActivity::setBanner(const char* text) {
  snprintf(banner, sizeof(banner), "%s", text ? text : "");
  bannerUntil = millis() + kBannerMs;
}

bool BluetoothSettingsActivity::ensureAvailable() {
  bleinput::StartResult result;
  {
    RenderLock lock;
    result = bleinput::ensureStarted(renderer, bleinput::StartContext::Explicit);
  }
  if (result == bleinput::StartResult::Started || result == bleinput::StartResult::AlreadyRunning) return true;
  setBanner(result == bleinput::StartResult::LowMemory ? tr(STR_BT_LOW_MEMORY) : tr(STR_BT_UNAVAILABLE));
  requestUpdate();
  return false;
}

void BluetoothSettingsActivity::rebuildRows() {
  rowCount = 0;
  const auto addRow = [this](const char* label, const int16_t value) -> fui::ListItem* {
    if (rowCount >= kMaxRows) return nullptr;
    rowItems[rowCount] = {};
    rowItems[rowCount].label = label;
    rowItems[rowCount].actionValue = rowCount;
    rowValues[rowCount] = value;
    return &rowItems[rowCount++];
  };

  if (view == View::Menu) {
    if (auto* row = addRow(tr(STR_BLUETOOTH), static_cast<int16_t>(Action::Toggle))) {
      row->toggle = true;
      row->toggleChecked = SETTINGS.bluetoothEnabled;
    }
    if (!SETTINGS.bluetoothEnabled) return;
    addRow(tr(STR_BT_SCAN_PAIR), static_cast<int16_t>(Action::Scan));
    if (BleHid.isConnected()) {
      if (auto* row = addRow(tr(STR_BT_DISCONNECT), static_cast<int16_t>(Action::Disconnect))) {
        row->value = BleHid.connectedName();
      }
    }
    addRow(tr(STR_BT_PAIRED_DEVICES), static_cast<int16_t>(Action::Paired));
    addRow(tr(STR_BT_MAP_BUTTONS), static_cast<int16_t>(Action::Map));
    return;
  }

  const uint8_t count = view == View::Scan ? BleHid.deviceCount() : BleHid.pairedCount();
  for (uint8_t i = 0; i < count; ++i) {
    addRow(view == View::Scan ? BleHid.device(i).name : BleHid.paired(i).name, i);
  }
  if (count != 0) return;
  if (view == View::Scan && !BleHid.isScanning()) {
    addRow(tr(STR_BT_NO_DEVICES), -1);
  } else if (view == View::Scan) {
    if (auto* row = addRow(tr(STR_SCANNING), -1)) row->enabled = false;
  } else if (auto* row = addRow(tr(STR_BT_NO_PAIRED), -1)) {
    row->enabled = false;
  }
}

void BluetoothSettingsActivity::enterScanView() {
  if (!ensureAvailable()) return;
  view = View::Scan;
  awaitingConnect = false;
  BleHid.startScan(kScanMs);
  moveSelectionTo(0);
}

void BluetoothSettingsActivity::connectTo(const char* address) {
  awaitingConnect = false;
  if (!ensureAvailable()) return;
  if (!BleHid.connect(address)) {
    setBanner(tr(STR_BT_CONNECTION_FAILED));
    return;
  }
  awaitingConnect = true;
  setBanner(tr(STR_CONNECTING));
  requestUpdate();
}

void BluetoothSettingsActivity::forgetPaired(const int index) {
  if (index < 0 || index >= BleHid.pairedCount()) return;
  char address[18];
  snprintf(address, sizeof(address), "%s", BleHid.paired(static_cast<uint8_t>(index)).addr);
  BleHid.forget(address);
  setBanner(tr(STR_BT_DEVICE_FORGOTTEN));
  moveSelectionTo(0);
}

void BluetoothSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount) return;
  app.clearTapFlash();
  const int16_t value = rowValues[index];

  if (view == View::Menu) {
    switch (static_cast<Action>(value)) {
      case Action::Toggle:
        SETTINGS.bluetoothEnabled = !SETTINGS.bluetoothEnabled;
        SETTINGS.saveToFile();
        if (SETTINGS.bluetoothEnabled)
          ensureAvailable();
        else
          bleinput::stop();
        requestUpdate();
        return;
      case Action::Scan:
        enterScanView();
        return;
      case Action::Paired:
        view = View::Paired;
        moveSelectionTo(0);
        return;
      case Action::Map:
        startActivityForResultWith<BleButtonMapActivity>([this](const ActivityResult&) { requestUpdate(); });
        return;
      case Action::Disconnect:
        BleHid.disconnect();
        setBanner(tr(STR_BT_NOT_CONNECTED));
        requestUpdate();
        return;
    }
  }

  if (view == View::Scan) {
    if (value < 0) {
      if (!BleHid.isScanning()) enterScanView();
    } else if (!awaitingConnect && value < BleHid.deviceCount()) {
      if (BleHid.isScanning()) BleHid.stopScan();
      connectTo(BleHid.device(static_cast<uint8_t>(value)).addr);
    }
    return;
  }
  if (!awaitingConnect && value >= 0 && value < BleHid.pairedCount()) {
    connectTo(BleHid.paired(static_cast<uint8_t>(value)).addr);
  }
}

void BluetoothSettingsActivity::onRowLongPress(const int index) {
  if (view == View::Paired && index >= 0 && index < rowCount) forgetPaired(rowValues[index]);
}

void BluetoothSettingsActivity::onBackButton() {
  if (view == View::Menu) {
    finish();
    return;
  }
  if (BleHid.isScanning()) BleHid.stopScan();
  view = View::Menu;
  moveSelectionTo(0);
}

bool BluetoothSettingsActivity::handleButtons() {
  if (view == View::Paired) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      if (!pairedHoldActionTaken && mappedInput.getHeldTime() >= kForgetHoldMs && nav.selected >= 0 &&
          nav.selected < rowCount) {
        forgetPaired(rowValues[nav.selected]);
        pairedHoldActionTaken = true;
      }
      return true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      const bool consumed = pairedHoldActionTaken;
      pairedHoldActionTaken = false;
      if (!consumed && nav.selected >= 0 && nav.selected < rowCount) activateIndex(nav.selected);
      return true;
    }
  }
  return UiListActivity::handleButtons();
}

bool BluetoothSettingsActivity::handleCustomInput() {
  if (bannerUntil && millis() > bannerUntil) {
    banner[0] = '\0';
    bannerUntil = 0;
    requestUpdate();
  }

  if (awaitingConnect) {
    char reason[48];
    if (BleHid.isConnected()) {
      awaitingConnect = false;
      BleHid.releaseScanResults();
      view = View::Menu;
      snprintf(banner, sizeof(banner), tr(STR_BT_CONNECTED_TO), BleHid.connectedName());
      bannerUntil = millis() + kBannerMs;
      moveSelectionTo(0);
    } else if (BleHid.takeConnectFailure(reason, sizeof(reason))) {
      LOG_ERR("BLEUI", "connect failed: %s", reason);
      awaitingConnect = false;
      setBanner(tr(STR_BT_CONNECTION_FAILED));
      requestUpdate();
    }
  }

  if (view == View::Scan && (renderedScanning != BleHid.isScanning() || renderedDeviceCount != BleHid.deviceCount())) {
    requestUpdate();
  }
  return false;
}

void BluetoothSettingsActivity::drawChrome() {
  UiListActivity::drawChrome();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const char* status = banner[0] ? banner : BleHid.isConnected() ? BleHid.connectedName() : tr(STR_BT_NOT_CONNECTED);
  GUI.drawSubHeader(renderer,
                    Rect{0, metrics.topPadding + metrics.headerHeight, renderer.getScreenWidth(), metrics.tabBarHeight},
                    status);
}

void BluetoothSettingsActivity::buildScreen(UiScreen& screen) {
  rebuildRows();
  renderedScanning = BleHid.isScanning();
  renderedDeviceCount = BleHid.deviceCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = screen.theme().spaceMd;
  syncListViewport(screen, props);
  screen.list(props);
}

#endif
