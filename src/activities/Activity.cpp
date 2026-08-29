#include "Activity.h"

#include "ActivityManager.h"
#include "CrossPointSettings.h"
#include "I18n.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

bool Activity::usesMainTabBar() const { return UITheme::getInstance().hasMainTabs() && mainTab() != MainTab::None; }

MappedInputManager::Labels Activity::mainTabButtonLabels(const char* back, const char* confirm, const bool canMove,
                                                         const bool showTabDirections) const {
  if (!usesMainTabBar()) {
    return mappedInput.mapLabels(back, confirm, canMove ? tr(STR_DIR_UP) : "", canMove ? tr(STR_DIR_DOWN) : "");
  }

  if (activityManager.getMainTabFocus() == MainTabFocus::Tabs) {
    const char* tabBack =
        mainTab() == MainTab::Recent ? (SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "") : tr(STR_BACK);
    return mappedInput.mapLabels(tabBack, tr(STR_SELECT), showTabDirections ? tr(STR_DIR_LEFT) : "",
                                 showTabDirections ? tr(STR_DIR_RIGHT) : "");
  }

  return mappedInput.mapLabels(tr(STR_BACK), confirm, canMove ? tr(STR_DIR_UP) : "", canMove ? tr(STR_DIR_DOWN) : "");
}

bool Activity::showMainTabContentSelection() const {
  return UITheme::getInstance().showSelectionCursor() &&
         (!usesMainTabBar() || activityManager.getMainTabFocus() == MainTabFocus::Content);
}

void Activity::drawPageHeader(const Rect& rect, const char* title, const char* subtitle) const {
  if (usesMainTabBar()) {
    GUI.drawMainTabBar(renderer, rect, mainTab());
  } else {
    GUI.drawHeader(renderer, rect, title, subtitle);
  }
}

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }

Activity::ListTouchResult Activity::handleListTouch(int& selectedIndex, const int itemCount, const int listTop,
                                                    const int listHeight, const bool hasSubtitle) {
  int touched = -1;
  if (mappedInput.wasListItemTouchedDown(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    if (selectedIndex != touched) {
      selectedIndex = touched;
      requestUpdate();
    }
    return ListTouchResult::Consumed;
  }
  if (mappedInput.wasListItemTapped(touched, itemCount, selectedIndex, listTop, listHeight, hasSubtitle)) {
    selectedIndex = touched;
    return ListTouchResult::Activated;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int pageItems = GUI.getListPageItems(listHeight, hasSubtitle);
    const int next = swipe == MappedInputManager::SwipeDir::Up
                         ? ButtonNavigator::nextPageIndex(selectedIndex, itemCount, pageItems)
                         : ButtonNavigator::previousPageIndex(selectedIndex, itemCount, pageItems);
    if (next != selectedIndex) {
      selectedIndex = next;
      requestUpdate();
    }
    return ListTouchResult::Consumed;
  }
  return ListTouchResult::None;
}
