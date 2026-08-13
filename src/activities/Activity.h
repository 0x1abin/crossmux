#pragma once
#include <Logging.h>
#include <Memory.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "activities/MainTab.h"
#include "util/ScreenshotInfo.h"

struct Rect;

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool isReaderActivity() const { return false; }
  // True if this activity needs the BLE stack resident (beyond the readers, which are
  // covered by isReaderActivity()). The Bluetooth settings screen overrides this so
  // pairing/scanning works there. Everywhere else BLE is torn down to free heap.
  virtual bool keepsBluetoothAlive() const { return false; }
  // True while the current activity is doing heap-heavy work that must finish
  // before the BLE stack (~52 KB) may start.
  virtual bool deferBluetoothStart() const { return false; }
  // Ask the activity to make its next render a full ghost-cleanup (HALF) refresh rather
  // than a fast/partial one. Used after drawing a transient popup over grayscale content
  // (e.g. the "BT Connecting..." popup over a reader page) so it clears without ghosting.
  virtual void requestGhostCleanup() {}
  // Returns true when the activity schedules its own forced refresh.
  virtual bool handleForcedRefresh() { return false; }
  virtual bool isHomeActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  virtual MainTab mainTab() const { return MainTab::None; }
  virtual bool mainTabBackReturnsToTabs() const { return true; }
  virtual void selectMainTabContentEdge(MainTabContentEdge) {}
  bool usesMainTabBar() const;
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  template <typename T, typename... Args>
  bool startActivityForResultWith(ActivityResultHandler resultHandler, Args&&... args) {
    // The child is owned by ActivityManager across frames, so stack storage is
    // not possible; keep its one allocation fallible.
    auto activity = makeUniqueNoThrow<T>(renderer, mappedInput, std::forward<Args>(args)...);
    if (!activity) {
      LOG_ERR("ACT", "OOM: child activity (%u bytes)", static_cast<unsigned>(sizeof(T)));
      return false;
    }
    startActivityForResult(std::move(activity), std::move(resultHandler));
    return true;
  }

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience method to facilitate API transition to ActivityManager
  // TODO: remove this in near future
  void onGoHome(HomeMenuItem item = HomeMenuItem::NONE);
  void onSelectBook(const std::string& path);

 protected:
  MappedInputManager::Labels mainTabButtonLabels(const char* back, const char* confirm, bool canMove,
                                                 bool showTabDirections = true) const;
  bool showMainTabContentSelection() const;
  void drawPageHeader(const Rect& rect, const char* title, const char* subtitle = nullptr) const;

  enum class ListTouchResult : uint8_t {
    None,      // touch did not hit the list
    Consumed,  // touchdown moved the highlight (repaint already requested)
    Activated  // tap landed on a row: selectedIndex is updated, caller activates it
  };

  // Shared touch handling for selectable list screens: touchdown highlights the
  // touched row, a tap selects and reports Activated. The caller supplies the
  // list band and runs its own activate action on Activated.
  ListTouchResult handleListTouch(int& selectedIndex, int itemCount, int listTop, int listHeight, bool hasSubtitle);
};
