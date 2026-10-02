"""Compile the production Wi-Fi teardown and deferred font recovery with host stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WifiFontRecoveryTest(unittest.TestCase):
    def test_deferred_locked_recovery_and_refresh(self):
        source = (ROOT / "src/main.cpp").read_text()
        start = source.index("#if FREEINK_CAP_TOUCH", source.index("static bool deepSleepInProgress"))
        teardown = source[start:source.index("void silentRestart()", start)]
        start = source.index("#if FREEINK_CAP_TOUCH && FREEINK_DEVICE_READPICO", source.index("void loop()"))
        recovery = source[start:source.index("  gpio.setSharedConfirmPowerShortPressEmitsPower", start)]
        harness = r'''
#include <cassert>
#include <initializer_list>
template<class... Args> void logDebug(Args...) {}
#define LOG_DBG(...) logDebug(__VA_ARGS__)
constexpr int WIFI_OFF = 0;
bool locked = false, touch = true, sntp = true;
int loads = 0, updates = 0, wifiStops = 0, sntpStops = 0;
int fontOutcome = 0; // Loaded, no selected font, or load failed.
bool fontLoaded = false;
int renderer;
struct RenderLock {
  RenderLock() { assert(!locked); locked = true; }
  ~RenderLock() { locked = false; }
};
struct BoardConfig { static bool hasTouch() { return touch; } };
bool esp_sntp_enabled() { return sntp; }
void esp_sntp_stop() { sntp = false; ++sntpStops; }
[[maybe_unused]] struct { void mode(int mode) { assert(mode == WIFI_OFF); ++wifiStops; } } WiFi;
void delay(int) {}
unsigned long millis() { return 0; }
[[maybe_unused]] struct {
  void ensureLoaded(int) {
    assert(locked);
    ++loads;
    fontLoaded = fontOutcome == 0;
  }
} sdFontSystem;
[[maybe_unused]] struct {
  void requestUpdate() {
    assert(!locked && loads == updates + 1);
    ++updates;
  }
} activityManager;
'''
        checks = r'''
int main() {
  recover();
  assert(loads == 0 && updates == 0);
#if FREEINK_CAP_TOUCH
  touch = false;
  assert(!finishWifiSessionWithoutRestart());
  recover();
  assert(wifiStops == 0 && sntpStops == 0 && loads == 0 && updates == 0);
  touch = true;
  for (int outcome : {0, 1, 2, 0}) {
    fontOutcome = outcome;
    const int before = loads;
    {
      RenderLock teardownLock;
      assert(finishWifiSessionWithoutRestart());
      assert(finishWifiSessionWithoutRestart()); // Coalesce pending requests.
      assert(loads == before && updates == before); // No I/O or nested lock in teardown.
    }
    recover();
#if FREEINK_DEVICE_READPICO
    assert(loads == before + 1 && updates == loads);
    assert(fontLoaded == (outcome == 0));
    assert(!sdFontReloadPending);
#else
    assert(loads == 0 && updates == 0);
#endif
    const int after = loads;
    recover();
    assert(loads == after && updates == after); // No retry every frame, even on failure.
  }
  assert(wifiStops == 8 && sntpStops == 1);
#endif
}
'''
        with tempfile.TemporaryDirectory(prefix="wifi-font-recovery-") as directory:
            cpp = Path(directory) / "check.cpp"
            binary = Path(directory) / "check"
            cpp.write_text(harness + teardown + "void recover() {\n" + recovery + "}\n" + checks)
            for touch_capability, readpico in ((1, 1), (1, 0), (0, 0), (0, 1)):
                with self.subTest(touch=touch_capability, readpico=readpico):
                    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                        "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-DFREEINK_CAP_TOUCH={touch_capability}",
                        f"-DFREEINK_DEVICE_READPICO={readpico}",
                        str(cpp), "-o", str(binary)
                    ], check=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
