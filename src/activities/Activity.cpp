#include "Activity.h"

#include <algorithm>

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

bool Activity::mainTabsAtBottom() const { return SETTINGS.inxTabPosition == CrossPointSettings::INX_TAB_BOTTOM; }

bool Activity::hasMainTabStatusBar() const {
  return MainTabs::showsStatusBar(usesMainTabBar(), mappedInput.hasTouch(), mainTabsAtBottom());
}

MainTabLayout Activity::mainTabLayout() const {
  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const bool tabsAtBottom = mainTabsAtBottom();
  const bool showStatus = hasMainTabStatusBar();
  Rect safe{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight() - metrics.buttonHintsHeight};
  if (showStatus) {
    safe = theme.getScreenSafeArea(renderer, true);
    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    const int safeRight = std::min(safe.x + safe.width, renderer.getScreenWidth() - right);
    const int safeBottom = std::min(safe.y + safe.height, renderer.getScreenHeight() - bottom);
    safe.x = std::max(safe.x, left);
    safe.y = std::max(safe.y, top);
    safe.width = std::max(0, safeRight - safe.x);
    safe.height = std::max(0, safeBottom - safe.y);
  }
  return MainTabs::layout(safe, metrics.topPadding, tabsAtBottom ? MainTabs::bottomBarHeight : metrics.headerHeight,
                          tabsAtBottom, showStatus ? MainTabs::statusBarHeight : 0);
}

Rect Activity::pageContentRect() const {
  if (usesMainTabBar()) return mainTabLayout().content;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight;
  return Rect{0, top, renderer.getScreenWidth(), renderer.getScreenHeight() - top - metrics.buttonHintsHeight};
}

void Activity::drawPageHeader(const Rect& rect, const char* title, const char* subtitle) const {
  if (usesMainTabBar()) {
    const MainTabLayout layout = mainTabLayout();
    GUI.drawMainTabBar(renderer, layout.tabBar, mainTab());
    if (layout.statusBar.height > 0) GUI.drawMainTabStatusBar(renderer, layout.statusBar);
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
