#include "ActivityManager.h"

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Memory.h>
#include <VectorFontSupport.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "apps/2048/Game2048Activity.h"
#include "apps/AppsMenuActivity.h"
#include "apps/airpage/AirPageActivity.h"
#include "apps/avatar/UglyAvatarActivity.h"
#include "apps/buddy/BuddyActivity.h"
#include "apps/calculator/CalculatorActivity.h"
#include "apps/sokoban/SokobanGameActivity.h"
#include "components/SubpageLayout.h"
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
#include "components/HeaderBackTapTarget.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/InxRecentActivity.h"
#include "home/RecentBooksActivity.h"
#include "library/LibraryListActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "network/UsbDriveActivity.h"
#include "plugins/PluginCatalogActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/FrontlightPanelActivity.h"
#include "util/FullScreenMessageActivity.h"
#include "util/ImageViewerActivity.h"
#include "util/UserGuide.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

extern HalGPIO gpio;

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
#if CROSSPOINT_VECTOR_FONTS || FREEINK_DEVICE_EEGO_A4
  // FreeType rasterization runs on this task, and the deepest observed chain
  // is a glyph fault DURING LAYOUT: expat + parser + line-layout frames
  // (~3.5KB on Xtensa) with the scan converter's FT_RENDER_POOL_SIZE (4KB)
  // stack-resident band pool on top — a measured ~8KB peak that trips the
  // canary on an 8KB stack. Vector-font boards all have PSRAM-class RAM.
  constexpr uint32_t renderTaskStackBytes = 16384;
#else
  constexpr uint32_t renderTaskStackBytes = 8192;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          renderTaskStackBytes,  // Stack size
                          this,                  // Parameters
                          1,                     // Priority
                          &renderTaskHandle,     // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  auto waitTicks = portMAX_DELAY;
  uint32_t idleGeneration = 0;
  while (true) {
    const bool foreground = ulTaskNotifyTake(pdTRUE, waitTicks) != 0;
    // ponytail: one idle attempt per render, including cancellations and I/O
    // failures; re-arm after quiet input only if measured cache hit rate needs it.
    waitTicks = portMAX_DELAY;
    RenderLock lock;
    if (currentActivity && pendingAction.load() == PendingAction::None) {
      HalPowerManager::Lock powerLock;
      if (foreground) {
        // Some renderers release the lock, so read activity-owned metadata
        // before calling them. Input during the render also cancels its idle pass.
        const auto delayMs = currentActivity->idleRenderDelayMs();
        idleGeneration = idleRenderGeneration.load(std::memory_order_relaxed);
        display.setInverted(SETTINGS.screenInverted != 0);
        currentActivity->render(std::move(lock));
        if (delayMs != 0) waitTicks = pdMS_TO_TICKS(delayMs);
      } else if (!idleRenderCancelled(idleGeneration)) {
        currentActivity->renderIdle(idleGeneration);
      }
    }
    // An idle timeout must never acknowledge requestUpdateAndWait(): its
    // notification may have arrived while the idle callback was cancelling.
    if (!foreground) continue;
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
  if (mappedInput.consumeSuppressedRelease()) {
    resetHomeStandbyInput();
    return;
  }

  if (currentActivity && currentActivity->requiresExclusiveStorageLoop()) {
    currentActivity->loop();
    // An exclusive-storage activity must restart rather than navigate away:
    // processing a pending action here could re-enable filesystem users while
    // the USB host still owns the raw SD card.
    if (requestedUpdate.exchange(false) && renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
    return;
  }

  if (currentActivity && pendingAction.load() == PendingAction::None) {
    if (handleMainTabInput()) {
      resetHomeStandbyInput();
      return;
    }
    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      resetHomeStandbyInput();
      if (currentActivity->handleHomeGesture()) {
        return;
      }
      goHome();
      return;
    }

    // Touch users can also open the global control center from the status bar.
    bool statusBarTap = false;
    if (mappedInput.hasTouch()) {
      int tx = 0;
      int ty = 0;
      if (currentActivity->usesMainTabBar()) {
        const Rect status = currentActivity->mainTabLayout().statusBar;
        statusBarTap = mappedInput.wasScreenTapped(tx, ty) && tx >= status.x && tx < status.x + status.width &&
                       ty >= status.y && ty < status.y + status.height;
      } else if (currentActivity->name == "Home" || currentActivity->name == "FileBrowser" ||
                 currentActivity->name == "Settings" || currentActivity->name == "NetworkModeSelection") {
        statusBarTap =
            mappedInput.wasScreenTapped(tx, ty) && ty >= 0 && ty < 44 && !HeaderBackTapTarget::contains(tx, ty);
      }
    }
    if (currentActivity->name != "FrontlightPanel" && (statusBarTap || mappedInput.wasLightPanelGesture())) {
      resetHomeStandbyInput();
      auto panel = makeUniqueNoThrow<FrontlightPanelActivity>(renderer, mappedInput);
      if (!panel) {
        LOG_ERR("ACT", "OOM: frontlight panel (%u bytes)", static_cast<unsigned>(sizeof(FrontlightPanelActivity)));
        return;
      }
      pushActivity(std::move(panel));
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    if (!handleHomeStandbyInput()) currentActivity->loop();
  }

  while (pendingAction.load() != PendingAction::None) {
    if (pendingAction.load() == PendingAction::Pop) {
      if (RenderLock::peek()) break;
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction.store(PendingAction::None);
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction.store(PendingAction::None);

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        resetHomeStandbyInput();
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
        if (pendingAction.load() == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      if (RenderLock::peek()) break;
      RenderLock lock;

      if (pendingAction.load() == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction.load() == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        // The parent's header back rect must not route taps on the pushed
        // screen (which may draw no header of its own).
        HeaderBackTapTarget::clear();
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction.store(PendingAction::None);
      currentActivity = std::move(pendingActivity);
      resetHomeStandbyInput();

      // Drop any one-shot tap/release edge events the outgoing activity already
      // consumed this frame. The SDK's InputManager clears these in update(),
      // but a pushActivity runs mid-frame; without this, the incoming activity
      // re-reads the same tap and double-activates (observed crash with WeRead:
      // the second activation hit WiFi/render-lock interleaving and tripped
      // FreeRTOS xTaskPriorityDisinherit on the rendering mutex).
#if !defined(SIMULATOR)
      gpio.clearTouchTapEvent();
#endif

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (pendingAction.load() == PendingAction::None && requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::resetHomeStandbyInput() {
  standbyBackState = mappedInput.isPressed(MappedInputManager::Button::Back) ? StandbyBackState::WaitingForRelease
                                                                             : StandbyBackState::Idle;
}

bool ActivityManager::handleHomeStandbyInput() {
  const bool eligible =
      currentActivity && (currentActivity->isHomeActivity() ||
                          (currentActivity->usesMainTabBar() && currentActivity->mainTab() == MainTab::Recent &&
                           mainTabFocus == MainTabFocus::Tabs));
  if (!eligible || !SETTINGS.standbyShortcutEnabled) {
    resetHomeStandbyInput();
    return false;
  }

  const bool pressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  const bool released = mappedInput.wasReleased(MappedInputManager::Button::Back);
  // Touch Back gestures publish a complete pair in one frame, independent of
  // an inherited physical hold. They remain usable through the release barrier.
  if (pressed && released) {
    standbyBackState = StandbyBackState::Idle;
    goToStandby();
    return true;
  }

  switch (standbyBackState) {
    case StandbyBackState::Idle:
      if (pressed) standbyBackState = StandbyBackState::Pressed;
      break;
    case StandbyBackState::Pressed:
      if (released) {
        standbyBackState = StandbyBackState::Idle;
        goToStandby();
        return true;
      }
      if (!mappedInput.isPressed(MappedInputManager::Button::Back)) standbyBackState = StandbyBackState::Idle;
      break;
    case StandbyBackState::WaitingForRelease:
      if (!mappedInput.isPressed(MappedInputManager::Button::Back)) standbyBackState = StandbyBackState::Idle;
      break;
  }
  // Home Activities no longer handle Back themselves; ignored releases must
  // still allow independent touch input, including on the release frame.
  return false;
}

bool ActivityManager::handleMainTabInput() {
  if (!currentActivity || !currentActivity->usesMainTabBar()) return false;

  const MainTab currentTab = currentActivity->mainTab();
  const Rect tabBar = currentActivity->mainTabLayout().tabBar;
  const auto insideTabs = [&tabBar](const int x, const int y) {
    return x >= tabBar.x && x < tabBar.x + tabBar.width && y >= tabBar.y && y < tabBar.y + tabBar.height;
  };

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    if (insideTabs(x, y)) {
      const MainTab target = MainTabs::fromX(x - tabBar.x, tabBar.width);
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

  if (mainTabFocus == MainTabFocus::Tabs && mappedInput.wasScreenTouchDown(x, y) && !insideTabs(x, y)) {
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

      // Recent's Back is owned by the shared home Standby handler.
      if (currentTab == MainTab::Recent) return false;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        goToMainTab(MainTabs::backTarget(currentTab));
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
  // The outgoing screen's header back button must not eat taps on the next
  // screen; the next header draw re-records it.
  HeaderBackTapTarget::clear();
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  cancelIdleRender();
  mappedInput.resetHomeButtonInput();
  standbyBackState = StandbyBackState::Idle;
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    resetHomeStandbyInput();
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() { replaceActivityWith<CrossPointWebServerActivity>(); }

void ActivityManager::goToJoinNetwork() {
  // Post heap-defrag reboot: enter the web-server activity straight in Join
  // Network mode (skips mode selection, does not reboot again).
  replaceActivityWith<CrossPointWebServerActivity>(/*startInJoinNetwork=*/true);
}

void ActivityManager::goToUsbDrive() {
#if FREEINK_CAP_USB_MSC
  auto activity = makeUniqueNoThrow<UsbDriveActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: USB Drive activity");
    return;
  }
  replaceActivity(std::move(activity));
#else
  LOG_ERR("ACT", "USB Drive requested in a build without USB Drive capability");
#endif
}

void ActivityManager::goToSettings() { replaceActivityWith<SettingsActivity>(); }

void ActivityManager::goToUglyAvatar() { replaceActivityWith<UglyAvatarActivity>(); }

void ActivityManager::goToFileBrowser(std::string path) { replaceActivityWith<FileBrowserActivity>(std::move(path)); }

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

void ActivityManager::goToLibrary() {
  auto activity = makeUniqueNoThrow<LibraryListActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("ACT", "OOM: library activity");
    return;
  }
  replaceActivity(std::move(activity));
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

void ActivityManager::goToPlugins(bool showOpds) {
  replaceActivityWith<PluginCatalogActivity>(showOpds, /*rootMode=*/true);
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  if (path.empty()) {
    goToFileBrowser("/");
    return;
  }
  if (FsHelpers::hasBmpExtension(path) || FsHelpers::hasPngExtension(path)) {
    replaceActivityWith<ImageViewerActivity>(std::move(path));
    return;
  }
  auto activity = ReaderActivity::create(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  if (activity) replaceActivity(std::move(activity));
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
  if (!CrossPointSettings::requiresOnboarding(SETTINGS.onboardingVersion)) {
    UserGuide::installIfPending(static_cast<Language>(SETTINGS.language) == Language::ZH_CN);
  }
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
    } else if (activityName == "Library") {
      initialMenuItem = HomeMenuItem::LIBRARY;
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

void ActivityManager::goToPixelSwitch() { replaceActivityWith<PixelSwitchActivity>(); }

void ActivityManager::goToCalculator() { replaceActivityWith<CalculatorActivity>(); }

void ActivityManager::goToWoodfish() { replaceActivityWith<WoodfishActivity>(); }

void ActivityManager::goToGame2048() { replaceActivityWith<Game2048Activity>(); }

void ActivityManager::goToAirPage() { replaceActivityWith<AirPageActivity>(); }

void ActivityManager::goToBuddy() { replaceActivityWith<BuddyActivity>(); }

void ActivityManager::goToStandby() { replaceActivityWith<StandbyActivity>(); }

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToChineseChess() { replaceActivityWith<ChineseChessMenuActivity>(); }
#endif

#ifdef ENABLE_CHINESE_VERSION
void ActivityManager::goToWeRead() { replaceActivityWith<WeReadActivity>(); }
#endif

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  cancelIdleRender();
  mappedInput.resetHomeButtonInput();
  standbyBackState = StandbyBackState::Idle;
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  cancelIdleRender();
  mappedInput.resetHomeButtonInput();
  standbyBackState = StandbyBackState::Idle;
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::requiresExclusiveStorageLoop() const {
  return currentActivity && currentActivity->requiresExclusiveStorageLoop();
}

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::keepsBluetoothAlive() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->keepsBluetoothAlive(); }) ||
         (currentActivity && currentActivity->keepsBluetoothAlive());
}

bool ActivityManager::deferBluetoothStart() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->deferBluetoothStart(); }) ||
         (currentActivity && currentActivity->deferBluetoothStart());
}

bool ActivityManager::handleForcedRefresh() {
  cancelIdleRender();
  return currentActivity && currentActivity->handleForcedRefresh();
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::prepareForSleep() {
  RenderLock lock;
  for (const auto& activity : stackActivities) activity->prepareForSleep();
  if (currentActivity) currentActivity->prepareForSleep();
}

void ActivityManager::requestUpdate(bool immediate) {
  cancelIdleRender();
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
  cancelIdleRender();
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  bool holdingRenderLock = (xSemaphoreGetMutexHolder(renderingMutex) == currTaskHandler);
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

RenderLock::RenderLock(Mode mode) {
  isLocked = xSemaphoreTake(activityManager.renderingMutex, mode == Mode::Try ? 0 : portMAX_DELAY) == pdTRUE;
  assert((mode == Mode::Try || isLocked) && "Blocking render lock acquisition failed");
}

RenderLock::RenderLock(Activity&) : RenderLock(Mode::Blocking) {}

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
