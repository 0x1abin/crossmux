#include "BleButtonMapActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>

#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

const BleButtonMapActivity::Function BleButtonMapActivity::kFunctions[kFunctionCount] = {
    {bleinput::Action::PageForward, StrId::STR_BT_PAGE_FORWARD},
    {bleinput::Action::PageBack, StrId::STR_BT_PAGE_BACK},
    {bleinput::Action::Confirm, StrId::STR_CONFIRM},
    {bleinput::Action::Back, StrId::STR_BACK},
    {bleinput::Action::Up, StrId::STR_DIR_UP},
    {bleinput::Action::Down, StrId::STR_DIR_DOWN},
    {bleinput::Action::Left, StrId::STR_DIR_LEFT},
    {bleinput::Action::Right, StrId::STR_DIR_RIGHT},
};

void BleButtonMapActivity::onEnter() {
  captureTarget = -1;
  UiListActivity::onEnter();
  const auto result = bleinput::ensureStarted(bleinput::StartContext::Explicit);
  if (result == bleinput::StartResult::LowMemory) unavailable = tr(STR_BT_LOW_MEMORY);
  if (result == bleinput::StartResult::Unavailable || result == bleinput::StartResult::Failed) {
    unavailable = tr(STR_BT_UNAVAILABLE);
  }
}

void BleButtonMapActivity::onExit() {
  mappedInput.setBleCaptureMode(false);
  Activity::onExit();
}

void BleButtonMapActivity::activateIndex(const int index) {
  if (unavailable || index < 0 || index >= kFunctionCount) return;
  app.clearTapFlash();
  captureTarget = index;
  mappedInput.setBleCaptureMode(true);
  requestUpdate();
}

bool BleButtonMapActivity::handleCustomInput() {
  if (captureTarget < 0) return false;
  uint8_t kind = 0xFF;
  uint8_t value = 0;
  if (mappedInput.takeCapturedBleKey(kind, value)) {
    if (!bleinput::assign(SETTINGS.bleKeyMap, kind, value, kFunctions[captureTarget].action)) {
      LOG_ERR("BLEUI", "cannot bind BLE key kind=%u value=%u", kind, value);
    } else {
      SETTINGS.saveToFile();
    }
    mappedInput.setBleCaptureMode(false);
    captureTarget = -1;
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    mappedInput.setBleCaptureMode(false);
    captureTarget = -1;
    requestUpdate();
  }
  return true;
}

void BleButtonMapActivity::onBackButton() { finish(); }

void BleButtonMapActivity::drawChrome() {
  UiListActivity::drawChrome();
  const char* prompt = unavailable ? unavailable : captureTarget >= 0 ? tr(STR_BT_PRESS_REMOTE) : nullptr;
  if (!prompt) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawSubHeader(renderer,
                    Rect{0, metrics.topPadding + metrics.headerHeight, renderer.getScreenWidth(), metrics.tabBarHeight},
                    prompt);
}

void BleButtonMapActivity::buildScreen(UiScreen& screen) {
  for (uint8_t i = 0; i < kFunctionCount; ++i) {
    rowItems[i] = {};
    rowItems[i].label = I18N.get(kFunctions[i].label);
    rowItems[i].actionValue = i;
    if (const auto* entry = bleinput::findByAction(SETTINGS.bleKeyMap, kFunctions[i].action)) {
      bleinput::describeKey(entry->keyKind, entry->keyValue, valueBuffers[i], sizeof(valueBuffers[i]));
    } else {
      snprintf(valueBuffers[i], sizeof(valueBuffers[i]), "%s", tr(STR_BT_NOT_MAPPED));
    }
    rowItems[i].value = valueBuffers[i];
    rowItems[i].enabled = unavailable == nullptr;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const bool hasSubHeader = unavailable || captureTarget >= 0;
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight +
                                                           (hasSubHeader ? metrics.tabBarHeight : 0)),
                                      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (captureTarget >= 0) {
    screen.centeredText(I18N.get(kFunctions[captureTarget].label));
    return;
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = kFunctionCount;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
