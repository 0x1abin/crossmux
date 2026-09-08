"""Keep menu dispatch aligned with Reader upstream da7feed5 on touch devices."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ControlCenterGestureTest(unittest.TestCase):
    def test_dispatch_and_input_ownership(self):
        source = (ROOT / "src/activities/ActivityManager.cpp").read_text()
        start = source.index("void ActivityManager::loop() {")
        end = source.index("  while (pendingAction.load() != PendingAction::None)", start)
        dispatch = source[start:end] + "}\n"
        harness = r'''
#include <atomic>
#include <cassert>
#include <memory>
#include <string>
#define LOG_ERR(...) ((void)0)
enum { eIncrement };
void xTaskNotify(int, int, int) {}
struct Activity {
  std::string name = "InxRecent";
  bool reader = false, exclusive = false;
  int loops = 0;
  bool requiresExclusiveStorageLoop() { return exclusive; }
  bool isReaderActivity() { return reader; }
  bool isHomeActivity() { return false; }
  bool handleHomeGesture() { return false; }
  void loop() { ++loops; }
};
struct Input {
  bool topSwipe = true, light = false, tap = false, suppressed = false;
  bool consumeSuppressedRelease() { return suppressed; }
  bool wasHomeGesture() { return false; }
  bool hasTouch() { return true; }
  bool wasScreenTapped(int&, int& y) { y = 10; return tap; }
  bool wasLightPanelGesture() { return light && topSwipe; }
  bool wasMenuGesture() { return topSwipe; }
};
struct FrontlightPanelActivity { FrontlightPanelActivity(int, Input&) {} };
bool failAllocation = false;
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return failAllocation ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct ActivityManager {
  enum class PendingAction { None, Push };
  std::atomic<PendingAction> pendingAction{PendingAction::None};
  std::atomic<bool> requestedUpdate{false};
  Activity* currentActivity;
  Input mappedInput;
  int renderer = 0, renderTaskHandle = 0, pushes = 0;
  bool handleMainTabInput() { return false; }
  void goHome() { assert(false); }
  void pushActivity(std::unique_ptr<FrontlightPanelActivity>) {
    ++pushes; pendingAction = PendingAction::Push;
  }
  void loop();
};
'''
        cases = r'''
int main() {
  const auto check = [](Activity page, Input input, bool opens, bool runs) {
    ActivityManager manager{.currentActivity = &page, .mappedInput = input};
    manager.loop();
    assert(manager.pushes == int(opens));
    assert(page.loops == int(runs)); // Opening must not also scroll/turn a page.
    if (opens) {
      manager.loop(); // A pending push cannot enqueue a second panel.
      assert(manager.pushes == 1 && page.loops == 0);
    }
  };
  for (bool light : {false, true})
    for (bool reader : {false, true})
      check({.reader = reader}, {.light = light}, light, !light);

  for (const char* name : {"InxRecent", "FileBrowser", "Settings", "Home", "FrontlightPanel"}) {
    const bool opens = std::string(name) != "FrontlightPanel";
    check({.name = name}, {.light = true}, opens, !opens);
  }
  for (const char* name : {"Home", "FileBrowser", "Settings", "NetworkModeSelection", "InxRecent", "EpubReader", "FrontlightPanel"}) {
    const std::string page(name);
    const bool opens = page == "Home" || page == "FileBrowser" ||
                       page == "Settings" || page == "NetworkModeSelection";
    check({.name = name}, {.topSwipe = false, .tap = true}, opens, !opens);
  }
  check({}, {.light = true, .suppressed = true}, false, false);
  check({.exclusive = true}, {.light = true}, false, true);
  check({}, {.topSwipe = false}, false, true);
  failAllocation = true;
  check({}, {.light = true}, false, false); // OOM does not leak the gesture.
}

'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "check.cpp"
            exe = Path(directory) / "check"
            cpp.write_text(harness + dispatch + cases)
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
