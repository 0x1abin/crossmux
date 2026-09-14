"""Run the actual reader restart function with host platform stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class SilentRestartToReaderTest(unittest.TestCase):
    def test_reader_target_and_sleep_guard_on_all_devices(self):
        source = (ROOT / "src/main.cpp").read_text()
        targets = source.split("RTC_NOINIT_ATTR uint32_t silentRebootMagic;", 1)[1].split(
            "// How the device is coming back to life", 1)[0]
        functions = "void silentRestart()" + source.split("void silentRestart()", 1)[1].split(
            "void silentRestartToReaderAndPreloadChineseFont", 1)[0]
        harness = r'''
#include <cassert>
#include <cstdint>
#define RTC_NOINIT_ATTR
#define LOG_DBG(...) ((void)0)
#define tr(key) "Loading"
uint32_t silentRebootMagic;
bool deepSleepInProgress = false, hasTouch = false;
unsigned restarts = 0, popups = 0, wifiStops = 0, delayedMs = 0;
int renderer;
struct { void restart() { ++restarts; } } ESP;
struct { void drawPopup(int, const char*) { ++popups; } } GUI;
void delay(unsigned ms) { delayedMs += ms; }
#if FREEINK_CAP_TOUCH
bool finishWifiSessionWithoutRestart() {
  if (!hasTouch) return false;
  ++wifiStops;
  return true;
}
#endif
'''
        checks = r'''
int main() {
  for (bool touch : {false, true}) {
    hasTouch = touch;
    for (bool sleeping : {false, true}) {
      deepSleepInProgress = sleeping;
      for (bool suppress : {false, true}) {
        silentRebootMagic = 0;
        silentRebootTarget = static_cast<uint32_t>(SilentRebootTarget::Home);
        silentRebootFontPointSize = 24;
        restarts = popups = wifiStops = delayedMs = 0;
        silentRestartToReader(suppress);
        assert(wifiStops == 0);
        assert(restarts == !sleeping && popups == !sleeping);
        assert(delayedMs == (sleeping ? 0u : 50u));
        assert(silentRebootMagic == (sleeping ? 0u : SILENT_REBOOT_MAGIC));
        const auto expected = sleeping ? SilentRebootTarget::Home :
            suppress ? SilentRebootTarget::ReaderSuppressFontPrompt : SilentRebootTarget::Reader;
        assert(silentRebootTarget == static_cast<uint32_t>(expected));
        assert(silentRebootFontPointSize == (sleeping ? 24u : 0u));
      }
    }
  }
#if FREEINK_CAP_TOUCH
  deepSleepInProgress = false;
  hasTouch = true;
  restarts = popups = wifiStops = 0;
  silentRestart();
  assert(wifiStops == 1 && restarts == 0 && popups == 0);
#endif
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "restart.cpp"
            cpp.write_text("#include <initializer_list>\n" + harness + targets + functions + checks)
            executable = Path(directory) / "restart"
            for touch_capability in (0, 1):
                with self.subTest(touch_capability=touch_capability):
                    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                        "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-DFREEINK_CAP_TOUCH={touch_capability}", str(cpp), "-o", str(executable)
                    ], check=True, capture_output=True)
                    subprocess.run([str(executable)], check=True, capture_output=True)
