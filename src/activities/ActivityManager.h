#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "Logging.h"
#include "MappedInputManager.h"
#include "Memory.h"
#include "activities/MainTab.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU, APPS };

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;
  MainTabFocus mainTabFocus = MainTabFocus::Tabs;
  bool mainTabEntryReleasePending = false;

  void exitActivity(const RenderLock& lock);
  bool handleMainTabInput();

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  // Shared between the main task (core 0) and the render task (core 1), so it
  // must be atomic rather than a plain/volatile enum (FreeRTOS SMP data race).
  std::atomic<PendingAction> pendingAction{PendingAction::None};

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Lock to serialize rendering operations. Must only be used via RenderLock.
  // NOTE: implemented as a counting semaphore (max=1, initial=1) instead of a
  // priority-inheriting mutex. The render task (core 1) and main task (core 0)
  // hand this across cores, and FreeRTOS SMP's priority-inheritance give path
  // trips the xTaskPriorityDisinherit assert. A counting semaphore has no
  // priority inheritance, so cross-core take/give is assert-free.
  SemaphoreHandle_t renderingMutex = nullptr;

  // Current task holding the render lock. Since a counting semaphore has no
  // owner tracking (xSemaphoreGetMutexHolder returns NULL), RenderLock records
  // the holder here so requestUpdateAndWait() can still detect misuse.
  std::atomic<TaskHandle_t> renderLockHolder{nullptr};

  // Heartbeat advanced by the render task after every completed render. The
  // main task's loop() watches it to detect a dead render task and restart it,
  // so the panel is never left frozen/black after a crash. Atomic: read by the
  // main task, written by the render task (cross-core).
  std::atomic<uint32_t> renderHeartbeat{0};
  uint32_t lastRenderHeartbeatSeen = 0;
  unsigned long lastRenderRequestMs = 0;
  // Set by loop() when a render is requested, cleared by the render task after
  // it completes; the watchdog below keys off it. Atomic: cross-core shared.
  std::atomic<bool> renderRequestOutstanding{false};

  void restartRenderTask();

  // Cross-task render request flag. requestUpdate() may set it from any task;
  // loop() consumes and clears it with exchange(false).
  std::atomic<bool> requestedUpdate{false};

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateCounting(1, 1)) {
    assert(renderingMutex != nullptr && "Failed to create rendering semaphore");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // Activities must outlive the caller, so they cannot use stack storage. This
  // keeps the single required heap allocation fallible under -fno-exceptions.
  template <typename T, typename... Args>
  bool replaceActivityWith(Args&&... args) {
    auto activity = makeUniqueNoThrow<T>(renderer, mappedInput, std::forward<Args>(args)...);
    if (!activity) {
      LOG_ERR("ACT", "OOM: activity (%u bytes)", static_cast<unsigned>(sizeof(T)));
      return false;
    }
    replaceActivity(std::move(activity));
    return true;
  }

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToFileTransfer();
  void goToSettings();
  void goToUglyAvatar();
  void goToReadingStatsMenu();
  void goToReadingStats();
  void goToInxRecent();
  void goToMainTab(MainTab tab);
  void goToFileBrowser(std::string path = {});
  void goToRecentBooks();
  void goToBrowser();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void goToSleep(bool fromTimeout = false);
  void goToBoot();
  bool goToPostOtaBoot(bool allowAutoPreload);
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goToApps();
  void goToSudoku();
  void goToSokoban();
  void goToGomoku();
  void goToMinesweeper();
  void goToPixelSwitch();
  void goToCalculator();
  void goToWoodfish();
  void goToAirPage();
  void goToBuddy();
  void goToStandby();
  void goToGame2048();
#ifdef ENABLE_CHINESE_VERSION
  void goToChineseChess();
#endif
#ifdef ENABLE_CHINESE_VERSION
  void goToWeRead();
#endif
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE);
  MainTabFocus getMainTabFocus() const { return mainTabFocus; }

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool isReaderActivity() const;
  bool handleForcedRefresh();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // Returns true when a Push/Pop/Replace is waiting for the render lock.
  // The render task can call this to abort a long render early and let the
  // main task proceed with the activity switch.
  bool isSwitchPending() const { return pendingAction.load() != PendingAction::None; }

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
