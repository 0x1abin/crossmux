#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <HalPowerManager.h>
#include <Memory.h>

#include <algorithm>

#include "OpdsServerStore.h"
#include "apps/2048/Game2048Activity.h"
#include "apps/AppsMenuActivity.h"
#include "apps/airpage/AirPageActivity.h"
#include "apps/avatar/UglyAvatarActivity.h"
#include "apps/buddy/BuddyActivity.h"
#include "apps/calculator/CalculatorActivity.h"
#include "apps/sokoban/SokobanGameActivity.h"
#ifdef ENABLE_CHINESE_VERSION
#include "apps/chinese-chess/ChineseChessMenuActivity.h"
#endif
#ifdef ENABLE_CHINESE_VERSION
#include "apps/weread/WeReadActivity.h"
#endif
#include "apps/gomoku/GomokuMenuActivity.h"
#include "apps/minesweeper/MinesweeperMenuActivity.h"
#include "apps/pixel-switch/PixelSwitchActivity.h"
#include "apps/reading-stats/ReadingStatsActivity.h"
#include "apps/reading-stats/ReadingStatsMenuActivity.h"
#include "apps/standby/StandbyActivity.h"
#include "apps/sudoku/SudokuMenuActivity.h"
#include "apps/woodfish/WoodfishActivity.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/InxRecentActivity.h"
#include "home/RecentBooksActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FullScreenMessageActivity.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    if (handleMainTabInput()) return;

    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

bool ActivityManager::handleMainTabInput() {
  if (!currentActivity || !currentActivity->usesMainTabBar()) return false;

  const MainTab currentTab = currentActivity->mainTab();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int tabTop = metrics.topPadding;
  const int tabBottom = tabTop + metrics.headerHeight;

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    if (y >= tabTop && y < tabBottom) {
      const MainTab target = MainTabs::fromX(x, renderer.getScreenWidth());
      if (target != MainTab::None) {
        mainTabFocus = MainTabFocus::Content;
        if (target != currentTab)
          goToMainTab(target);
        else
          requestUpdate();
      }
      return true;
    }
    if (mainTabFocus == MainTabFocus::Tabs) {
      mainTabFocus = MainTabFocus::Content;
      requestUpdate();
    }
    return false;
  }

  if (mainTabFocus == MainTabFocus::Tabs && mappedInput.wasScreenTouchDown(x, y) && (y < tabTop || y >= tabBottom)) {
    mainTabFocus = MainTabFocus::Content;
    requestUpdate();
    return false;
  }

  if (mainTabEntryReleasePending) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down))
      mainTabEntryReleasePending = false;
    return true;
  }

  switch (mainTabFocus) {
    case MainTabFocus::Tabs:
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        goToMainTab(MainTabs::adjacent(currentTab, -1));
        return true;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        goToMainTab(MainTabs::adjacent(currentTab, 1));
        return true;
      }
      if (mappedInput.isPressed(MappedInputManager::Button::Left) ||
          mappedInput.isPressed(MappedInputManager::Button::Right)) {
        return true;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        mainTabFocus = MainTabFocus::Content;
        requestUpdate();
        return true;
      }
      if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) return true;

      if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        mainTabFocus = MainTabFocus::Content;
        mainTabEntryReleasePending = true;
        currentActivity->selectMainTabContentEdge(MainTabContentEdge::First);
        requestUpdate();
        return true;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        mainTabFocus = MainTabFocus::Content;
        mainTabEntryReleasePending = true;
        currentActivity->selectMainTabContentEdge(MainTabContentEdge::Last);
        requestUpdate();
        return true;
      }

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        const MainTab target = MainTabs::backTarget(currentTab);
        if (target != MainTab::None)
          goToMainTab(target);
        else if (SETTINGS.standbyShortcutEnabled)
          goToStandby();
        return true;
      }
      return mappedInput.isPressed(MappedInputManager::Button::Back);

    case MainTabFocus::Content:
      if (!currentActivity->mainTabBackReturnsToTabs()) return false;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        mainTabFocus = MainTabFocus::Tabs;
        requestUpdate();
        return true;
      }
      return mappedInput.isPressed(MappedInputManager::Button::Back);
  }
  return false;
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() {
  replaceActivityWith<CrossPointWebServerActivity>();
}

void ActivityManager::goToSettings() { replaceActivityWith<SettingsActivity>(); }

void ActivityManager::goToUglyAvatar() { replaceActivityWith<UglyAvatarActivity>(); }

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivityWith<FileBrowserActivity>(std::move(path));
}

void ActivityManager::goToRecentBooks() { replaceActivityWith<RecentBooksActivity>(); }

void ActivityManager::goToInxRecent() { replaceActivityWith<InxRecentActivity>(); }

void ActivityManager::goToMainTab(const MainTab tab) {
  mainTabEntryReleasePending = false;
  switch (tab) {
    case MainTab::Recent:
      goToInxRecent();
      return;
    case MainTab::Library:
      goToFileBrowser();
      return;
    case MainTab::Settings:
      goToSettings();
      return;
    case MainTab::Statistics:
      goToReadingStats();
      return;
    case MainTab::Apps:
      goToApps();
      return;
    case MainTab::None:
      return;
  }
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivityWith<OpdsBookBrowserActivity>(servers[0]);
  } else {
    replaceActivityWith<OpdsServerListActivity>(true);
  }
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  replaceActivityWith<ReaderActivity>(std::move(path), allowFastInitialRefresh);
}

void ActivityManager::goToSleep(bool fromTimeout) {
  if (replaceActivityWith<SleepActivity>(fromTimeout)) {
    loop();  // The caller sleeps immediately after this returns, so render now.
  }
}

void ActivityManager::goToBoot() { replaceActivityWith<BootActivity>(); }

bool ActivityManager::goToPostOtaBoot(bool allowAutoPreload) {
  return replaceActivityWith<BootActivity>(BootActivity::Mode::PostOta, allowAutoPreload);
}

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivityWith<FullScreenMessageActivity>(std::move(message), style);
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::INX) {
    mainTabFocus = MainTabFocus::Tabs;
    mainTabEntryReleasePending = false;
    goToInxRecent();
    return;
  }
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "RecentBooks") {
      initialMenuItem = HomeMenuItem::RECENTS;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivityWith<HomeActivity>(initialMenuItem);
}
void ActivityManager::goToCrashReport() { replaceActivityWith<CrashActivity>(); }

void ActivityManager::goToApps() { replaceActivityWith<AppsMenuActivity>(); }

void ActivityManager::goToReadingStatsMenu() { replaceActivityWith<ReadingStatsMenuActivity>(); }

void ActivityManager::goToReadingStats() { replaceActivityWith<ReadingStatsActivity>(true); }

void ActivityManager::goToSudoku() { replaceActivityWith<SudokuMenuActivity>(); }

void ActivityManager::goToSokoban() { replaceActivityWith<SokobanGameActivity>(); }

void ActivityManager::goToGomoku() { replaceActivityWith<GomokuMenuActivity>(); }

void ActivityManager::goToMinesweeper() { replaceActivityWith<MinesweeperMenuActivity>(); }

void ActivityManager::goToPixelSwitch() {
  replaceActivityWith<PixelSwitchActivity>();
}

void ActivityManager::goToCalculator() { replaceActivityWith<CalculatorActivity>(); }

void ActivityManager::goToWoodfish() { replaceActivityWith<WoodfishActivity>(); }

void ActivityManager::goToGame2048() { replaceActivityWith<Game2048Activity>(); }

void ActivityManager::goToAirPage() { replaceActivityWith<AirPageActivity>(); }

void ActivityManager::goToBuddy() { replaceActivityWith<BuddyActivity>(); }

void ActivityManager::goToStandby() { replaceActivityWith<StandbyActivity>(); }

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToChineseChess() {
  replaceActivityWith<ChineseChessMenuActivity>();
}
#endif

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToWeRead() {
  replaceActivityWith<WeReadActivity>();
}
#endif

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
