#include "AppVisibilitySettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/apps/AppsMenuActivity.h"
#include "components/UITheme.h"

void AppVisibilitySettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  dirty = false;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void AppVisibilitySettingsActivity::onExit() {
  if (dirty) SETTINGS.saveToFile();
  Activity::onExit();
}

void AppVisibilitySettingsActivity::loop() {
  if (waitForConfirmRelease) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease = false;
    }
    return;
  }

  const int appCount = AppsMenuActivity::getAppCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  switch (handleListTouch(selectedIndex, appCount, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      toggleSelected();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleSelected();
    return;
  }

  buttonNavigator.onNext([this, appCount] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, appCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, appCount] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, appCount);
    requestUpdate();
  });
}

void AppVisibilitySettingsActivity::toggleSelected() {
  const bool visible = AppsMenuActivity::isAppVisible(selectedIndex);
  if (AppsMenuActivity::setAppVisible(selectedIndex, !visible)) {
    dirty = true;
    requestUpdate();
  }
}

void AppVisibilitySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int appCount = AppsMenuActivity::getAppCount();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_APP_VISIBILITY));
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, appCount, selectedIndex,
      [](int index) { return std::string(I18N.get(AppsMenuActivity::getAppTitleId(index))); }, nullptr, nullptr,
      [](int index) {
        return AppsMenuActivity::isAppVisible(index) ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
