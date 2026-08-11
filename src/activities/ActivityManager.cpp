#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <HalGPIO.h>
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

extern HalGPIO gpio;  // defined in main.cpp

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

// How long the main task waits for the render task to advance its heartbeat
// before declaring it dead and restarting it. Generous to tolerate slow renders
// (cover decode, gray refresh) without false positives.
static constexpr unsigned long kRenderWatchdogTimeoutMs = 10000;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          16384,              // Stack size (16KB: TXT/Epub render paths prewarm fonts, decode
                                              // covers and run Bidi — a tight 8KB overflows and aborts the task,
                                              // leaving a black panel that the heartbeat then restarts forever)
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
    // Skip rendering when a Push/Pop/Replace is pending: the main task is
    // waiting to acquire this lock to swap currentActivity. Rendering the
    // old activity here would re-hold the lock for the entire render duration,
    // starving the main task and freezing the device.
    if (currentActivity && pendingAction == PendingAction::None) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      currentActivity->render(std::move(lock));
    }
    // Advance the heartbeat so the main task's watchdog can tell this task is
    // still alive (not crashed/aborted with a black, frozen panel).
    renderHeartbeat = static_cast<uint32_t>(renderHeartbeat + 1);
    renderRequestOutstanding = false;
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

void ActivityManager::restartRenderTask() {
  LOG_ERR("ACT", "Render task heartbeat stalled; restarting render task");
  renderRequestOutstanding = false;

  // The crashed task may have held the render semaphore when it died. Since a
  // counting semaphore has no owner to release, recreate it so no task is stuck
  // waiting forever.
  if (renderingMutex) {
    vSemaphoreDelete(renderingMutex);
  }
  renderingMutex = xSemaphoreCreateCounting(1, 1);
  assert(renderingMutex && "Failed to recreate rendering semaphore");
  renderLockHolder.store(nullptr);

#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif

  // Remove any leftover (dead) render task before creating a fresh one.
  if (renderTaskHandle) {
    vTaskDelete(renderTaskHandle);
    renderTaskHandle = nullptr;
  }
  renderHeartbeat = 0;
  lastRenderHeartbeatSeen = 0;

  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender", 16384, this, 1, &renderTaskHandle, renderTaskCore);
  assert(renderTaskHandle != nullptr && "Failed to recreate render task");

  // Re-render the current activity so the panel recovers from the black screen.
  renderRequestOutstanding = true;
  lastRenderRequestMs = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
  xTaskNotify(renderTaskHandle, 1, eIncrement);
}

void ActivityManager::loop() {
  if (currentActivity && pendingAction == PendingAction::None) {
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
      if (RenderLock::peek()) break;
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
      if (RenderLock::peek()) break;
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

      // Drop any one-shot tap/release edge events the outgoing activity already
      // consumed this frame. InputManager clears these in update(), but a
      // pushActivity runs mid-frame; without this, the incoming activity re-reads
      // the same tap and double-activates (observed crash with WeRead: the second
      // activation hit WiFi/render-lock interleaving and tripped FreeRTOS
      // xTaskPriorityDisinherit on the rendering mutex).
      gpio.clearTouchTapEvent();

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (pendingAction == PendingAction::None && requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
      // Record that a render is outstanding so the watchdog below can detect a
      // render task that crashed mid-render and abandoned the panel.
      lastRenderRequestMs = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
      renderRequestOutstanding = true;
    }
  }

  // Render task watchdog: if a render was requested but the render task never
  // advanced its heartbeat (it crashed/aborted), restart it so the panel is not
  // left frozen/black. A stalled low-power render that legitimately takes a while
  // is tolerated by the generous timeout.
  if (renderRequestOutstanding) {
    const unsigned long nowMs = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    // The heartbeat only advances AFTER render() returns, so a render that is
    // legitimately in progress (the render task is currently holding the render
    // lock) can run longer than the watchdog timeout. Use a two-tier timeout:
    //  - No lock held: short timeout (task died before acquiring lock)
    //  - Lock held: long timeout (legitimate long render OR crashed task)
    // The long timeout must exceed any legitimate render (AA grayscale: ~15s,
    // plus one 30s waitBusy timeout = ~45s worst case).
    constexpr unsigned long kRenderCrashTimeoutMs = 35000;  // 35s for lock-held crashes
    const bool heartbeatStalled = (renderHeartbeat == lastRenderHeartbeatSeen);
    if (heartbeatStalled) {
      const unsigned long effectiveTimeout =
          (renderLockHolder.load() != nullptr) ? kRenderCrashTimeoutMs : kRenderWatchdogTimeoutMs;
      if ((nowMs - lastRenderRequestMs) > effectiveTimeout) {
        restartRenderTask();
      }
    }
    lastRenderHeartbeatSeen = renderHeartbeat;
  } else {
    lastRenderHeartbeatSeen = renderHeartbeat;
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
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

void ActivityManager::goToUglyAvatar() { replaceActivity(std::make_unique<UglyAvatarActivity>(renderer, mappedInput)); }

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToRecentBooks() {
  replaceActivity(std::make_unique<RecentBooksActivity>(renderer, mappedInput));
}

void ActivityManager::goToInxRecent() {
  auto activity = makeUniqueNoThrow<InxRecentActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: InxRecentActivity (%u bytes)", static_cast<unsigned>(sizeof(InxRecentActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

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
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

bool ActivityManager::goToPostOtaBoot(bool allowAutoPreload) {
  // Activities outlive this call and are owned by ActivityManager, so this small allocation cannot use the stack.
  auto activity = makeUniqueNoThrow<BootActivity>(renderer, mappedInput, BootActivity::Mode::PostOta, allowAutoPreload);
  if (!activity) {
    LOG_ERR("ACT", "OOM: BootActivity (%u bytes)", static_cast<unsigned>(sizeof(BootActivity)));
    return false;
  }
  replaceActivity(std::move(activity));
  return true;
}

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
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
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::goToApps() { replaceActivity(std::make_unique<AppsMenuActivity>(renderer, mappedInput)); }

void ActivityManager::goToReadingStatsMenu() {
  replaceActivity(std::make_unique<ReadingStatsMenuActivity>(renderer, mappedInput));
}

void ActivityManager::goToReadingStats() {
  auto activity = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput, true);
  if (!activity) {
    LOG_ERR("ACT", "OOM: ReadingStatsActivity (%u bytes)", static_cast<unsigned>(sizeof(ReadingStatsActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToSudoku() { replaceActivity(std::make_unique<SudokuMenuActivity>(renderer, mappedInput)); }

void ActivityManager::goToSokoban() { replaceActivity(std::make_unique<SokobanGameActivity>(renderer, mappedInput)); }

void ActivityManager::goToGomoku() { replaceActivity(std::make_unique<GomokuMenuActivity>(renderer, mappedInput)); }

void ActivityManager::goToMinesweeper() {
  replaceActivity(std::make_unique<MinesweeperMenuActivity>(renderer, mappedInput));
}

void ActivityManager::goToPixelSwitch() {
  // ActivityManager owns this 1500-byte inline canvas across frames, so it
  // cannot live on the caller's stack. Failure must not abort under -fno-exceptions.
  auto activity = makeUniqueNoThrow<PixelSwitchActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: PixelSwitchActivity (%u bytes)", static_cast<unsigned>(sizeof(PixelSwitchActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToCalculator() {
  auto activity = makeUniqueNoThrow<CalculatorActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: CalculatorActivity (%u bytes)", static_cast<unsigned>(sizeof(CalculatorActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToWoodfish() {
  auto activity = makeUniqueNoThrow<WoodfishActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: WoodfishActivity (%u bytes)", static_cast<unsigned>(sizeof(WoodfishActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToGame2048() { replaceActivity(std::make_unique<Game2048Activity>(renderer, mappedInput)); }

void ActivityManager::goToAirPage() {
  // Activities must outlive this call; ActivityManager takes ownership of this
  // single allocation and releases it on navigation.
  auto activity = makeUniqueNoThrow<AirPageActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: AirPageActivity (%u bytes)", static_cast<unsigned>(sizeof(AirPageActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToBuddy() {
  auto activity = makeUniqueNoThrow<BuddyActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: BuddyActivity (%u bytes)", static_cast<unsigned>(sizeof(BuddyActivity)));
    return;
  }
  replaceActivity(std::move(activity));
}

void ActivityManager::goToStandby() { replaceActivity(std::make_unique<StandbyActivity>(renderer, mappedInput)); }

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToChineseChess() {
  replaceActivity(std::make_unique<ChineseChessMenuActivity>(renderer, mappedInput));
}
#endif

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToWeRead() {
  auto activity = makeUniqueNoThrow<WeReadActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: WeReadActivity");
    return;
  }
  replaceActivity(std::move(activity));
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
  // renderingMutex is now a counting semaphore (no priority inheritance), so
  // xSemaphoreGetMutexHolder() can no longer report the owner. RenderLock
  // records the holder in renderLockHolder instead.
  bool holdingRenderLock = (renderLockHolder.load() == currTaskHandler);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
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
  activityManager.renderLockHolder.store(xTaskGetCurrentTaskHandle());
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  activityManager.renderLockHolder.store(xTaskGetCurrentTaskHandle());
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    activityManager.renderLockHolder.store(nullptr);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    activityManager.renderLockHolder.store(nullptr);
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
