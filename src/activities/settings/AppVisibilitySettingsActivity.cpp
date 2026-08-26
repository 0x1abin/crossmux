#include "AppVisibilitySettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/apps/AppsMenuActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

AppVisibilitySettingsActivity::AppVisibilitySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("AppVisibilitySettings", renderer, mappedInput) {}

void AppVisibilitySettingsActivity::onEnter() {
  UiListActivity::onEnter();
  dirty = false;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  rowItems.clear();
  rowItems.reserve(static_cast<size_t>(listCount()));
  for (int i = 0; i < listCount(); ++i) {
    fui::ListItem item;
    item.label = I18N.get(AppsMenuActivity::getAppTitleId(i));
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void AppVisibilitySettingsActivity::onExit() {
  if (dirty) SETTINGS.saveToFile();
  Activity::onExit();
}

int AppVisibilitySettingsActivity::listCount() const { return AppsMenuActivity::getAppCount(); }

bool AppVisibilitySettingsActivity::handleCustomInput() {
  if (!waitForConfirmRelease) return false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
  return true;
}

void AppVisibilitySettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  app.clearTapFlash();
  const bool visible = AppsMenuActivity::isAppVisible(index);
  if (AppsMenuActivity::setAppVisible(index, !visible)) {
    dirty = true;
    requestUpdate();
  }
}

const char* AppVisibilitySettingsActivity::headerTitle() const { return tr(STR_APP_VISIBILITY); }

void AppVisibilitySettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < listCount(); ++i) {
    rowItems[static_cast<size_t>(i)].value = AppsMenuActivity::isAppVisible(i) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void AppVisibilitySettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
