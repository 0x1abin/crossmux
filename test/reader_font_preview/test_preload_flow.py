"""Run the production Text Settings exit flow against small host UI/I/O stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/activities/settings/TextSettingsActivity.cpp").read_text()
header = (ROOT / "src/activities/settings/TextSettingsActivity.h").read_text()


def method(name):
    start = source.index(f"TextSettingsActivity::{name}(")
    start = source.rfind("\n", 0, start) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def enum(name):
    start = header.index(f"enum class {name} ")
    return header[start:header.index(";", start) + 1]


harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <SdCardFontCache.h>
using Result = SdCardFontCache::Result;
unsigned long now = 100;
unsigned long millis() { return now; }
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
enum class StrId { STR_FONT_PRELOAD_START, STR_FONT_PRELOAD_SKIP, STR_FONT_PRELOAD_CONFIRM, Error };
constexpr StrId OK_OPTION[] = {StrId::Error};
namespace fontpreload {
constexpr unsigned long NOTICE_DURATION_MS = 2000;
StrId failureMessage(Result) { return StrId::Error; }
}
struct {
  char sdFontFamilyName[32] = "Sans";
  uint8_t sdFontFlashPreload = 0, fontPointSize = 18;
  void saveToFile() {}
} SETTINGS;
struct MappedInputManager {
  enum class Button { Back };
  static constexpr uint8_t kButtonCount = 1;
  bool held = false, touch = false;
  bool isScreenTouchHeld(int&, int&) const { return touch; }
  bool isPressed(Button) const { return held; }
};
struct SdCardFontFileInfo { std::string path = "Sans_18.cpfont"; };
struct Family { bool vector = false; };
struct Registry {
  Family family;
  const Family* findFamily(const char*) const { return &family; }
};
struct {
  int releases = 0;
  bool loadedFlash = false;
  void releaseLoadedFont(int) { ++releases; }
  void ensureLoaded(int, bool allow = true) { loadedFlash = allow && SETTINGS.sdFontFlashPreload; }
} sdFontSystem;
Result check = Result::Ok, outcome = Result::Ok;
int checks = 0;
namespace SdCardFontCache { Result preflight(const char*) { ++checks; return check; } }
struct Popup {
  bool active = false;
  int selected = -1, action = -2, shows = 0;
  std::function<void(int)> callback;
  void show(StrId, const StrId*, int, int index, std::function<void(int)> cb) {
    active = true; selected = index; callback = cb; ++shows;
  }
  bool isActive() const { return active; }
  bool handleInput(MappedInputManager&, const std::function<void()>& update) {
    if (!active) return false;
    if (action != -2) {
      active = false;
      if (action >= 0 && callback) callback(action);
      action = -2;
      update();
    }
    return true;
  }
};
class TextSettingsActivity {
 public:
''' + "\n".join(enum(n) for n in (
    "StartMode", "InitialFontState", "ExitDestination", "ExitPrompt", "FontLoadState"
)) + r'''
  Registry storage;
  const Registry* registry_ = &storage;
  SdCardFontFileInfo file;
  const SdCardFontFileInfo* fontFileForFamily(int, uint8_t) { return &file; }
  int renderer = 0, currentFamilyIndex_ = 1, initialFamilyIndex_ = 0, preloads = 0;
  uint8_t initialPointSize_ = 16, initialSdFontFlashPreload_ = 0;
  StartMode startMode_ = StartMode::Interactive;
  InitialFontState initialFontState_ = InitialFontState::Unchanged;
  ExitDestination exitDestination_ = ExitDestination::Previous;
  ExitPrompt exitPrompt_ = ExitPrompt::None;
  bool exitInProgress_ = false, exitPromptWaitForBackRelease_ = false;
  bool exited = false, home = false;
  unsigned long noticeStartedAt_ = 0;
  std::atomic<FontLoadState> fontLoadState_{FontLoadState::Idle};
  MappedInputManager mappedInput;
  Popup optionPopup_;
  void requestUpdate() {}
  void requestUpdateAndWait() { now += 500; } // Display time is not part of notice dwell.
  void finish() { exited = true; }
  void onGoHome() { exited = home = true; }
  Result preloadFont(const SdCardFontFileInfo&, const char*) { ++preloads; return outcome; }
  void exitAfterFinalFont(ExitDestination);
  void finishFinalFont(bool);
  void showPreloadFailure(Result);
  void completeExit();
  bool handleCustomInput();
  bool handleHomeGesture();
};
''' + "\n".join(method(n) for n in (
    "exitAfterFinalFont", "finishFinalFont", "showPreloadFailure", "completeExit",
    "handleCustomInput", "handleHomeGesture"
)) + r'''
int main() {
  using Exit = TextSettingsActivity::ExitDestination;
  using Mode = TextSettingsActivity::StartMode;
  for (int choice : {0, 1, -1}) { // Accept, skip, dismissal without callback (Back/outside).
    TextSettingsActivity a;
    a.exitAfterFinalFont(Exit::Previous);
    assert(a.optionPopup_.active && a.optionPopup_.selected == 0);
    assert(a.preloads == 0 && !a.exited && SETTINGS.sdFontFlashPreload == 0);
    a.optionPopup_.action = choice;
    a.handleCustomInput();
    assert(a.exited && a.preloads == (choice == 0));
    assert(SETTINGS.sdFontFlashPreload == (choice == 0));
  }
  TextSettingsActivity held;
  held.mappedInput.held = true;
  held.exitAfterFinalFont(Exit::Home);
  held.optionPopup_.action = 0;
  held.handleCustomInput();
  held.mappedInput.held = false;
  held.handleCustomInput();
  assert(held.preloads == 0); // Hold and its release are both consumed.
  held.handleCustomInput();
  assert(held.home && held.preloads == 1);

  check = Result::TooLarge;
  TextSettingsActivity large;
  large.exitAfterFinalFont(Exit::Previous);
  assert(!large.optionPopup_.active && large.preloads == 0 && !large.exited);
  now += fontpreload::NOTICE_DURATION_MS - 1;
  large.handleCustomInput();
  assert(!large.exited);
  ++now;
  large.mappedInput.held = true;
  large.handleCustomInput();
  assert(!large.exited);
  large.mappedInput.held = false;
  large.mappedInput.touch = true;
  large.handleCustomInput();
  assert(!large.exited);
  large.mappedInput.touch = false;
  large.handleCustomInput();
  assert(large.exited && SETTINGS.sdFontFlashPreload == 0);

  check = Result::AlreadyCached;
  TextSettingsActivity cached;
  cached.exitAfterFinalFont(Exit::Previous);
  assert(cached.exited && cached.preloads == 0 && cached.optionPopup_.shows == 0);
  assert(SETTINGS.sdFontFlashPreload == 1 && sdFontSystem.loadedFlash);

  for (Mode mode : {Mode::Interactive, Mode::PreviewOnly, Mode::AskThenExit, Mode::PreloadThenExit}) {
    check = Result::Ok;
    TextSettingsActivity a;
    a.startMode_ = mode;
    a.exitAfterFinalFont(Exit::Previous);
    assert(a.preloads == (mode == Mode::PreloadThenExit));
    assert(a.optionPopup_.active == (mode == Mode::Interactive || mode == Mode::AskThenExit));
  }
  TextSettingsActivity vector;
  vector.storage.family.vector = true;
  int before = checks;
  vector.exitAfterFinalFont(Exit::Previous);
  assert(vector.exited && checks == before && vector.preloads == 0);

  TextSettingsActivity restored;
  restored.currentFamilyIndex_ = restored.initialFamilyIndex_;
  restored.initialPointSize_ = SETTINGS.fontPointSize;
  restored.initialSdFontFlashPreload_ = 1;
  SETTINGS.sdFontFlashPreload = 0;
  restored.exitAfterFinalFont(Exit::Previous);
  assert(restored.exited && restored.optionPopup_.shows == 0 && restored.preloads == 0);
  assert(SETTINGS.sdFontFlashPreload == 1 && sdFontSystem.loadedFlash);

  TextSettingsActivity changed;
  changed.exitAfterFinalFont(Exit::Previous);
  check = Result::NotSafe; // Eligibility changed while the prompt was open.
  changed.optionPopup_.action = 0;
  changed.handleCustomInput();
  assert(changed.preloads == 0 && changed.optionPopup_.active);
  changed.optionPopup_.action = -1;
  changed.handleCustomInput();
  assert(changed.exited && SETTINGS.sdFontFlashPreload == 0);

  check = Result::Ok;
  outcome = Result::WriteFailed;
  TextSettingsActivity failed;
  failed.startMode_ = Mode::PreloadThenExit;
  failed.exitAfterFinalFont(Exit::Previous);
  assert(failed.preloads == 1 && failed.optionPopup_.active && !sdFontSystem.loadedFlash);
  failed.optionPopup_.action = -1;
  failed.handleCustomInput();
  assert(failed.exited && SETTINGS.sdFontFlashPreload == 0);
}
'''

with tempfile.TemporaryDirectory(prefix="font-preload-flow-") as tmp:
    cpp = Path(tmp) / "flow.cpp"
    exe = Path(tmp) / "flow"
    cpp.write_text(harness)
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "lib/EpdFont"),
        str(cpp), "-o", str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
print("Font preload exit flow passed")
