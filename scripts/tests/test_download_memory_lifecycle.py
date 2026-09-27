"""Compile the real download entry/exit methods with host lifecycle doubles."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


class DownloadMemoryLifecycleTest(unittest.TestCase):
    def test_save_failure_cancellation_and_exit_routes(self):
        ota = (ROOT / "src/activities/settings/OtaUpdateActivity.cpp").read_text()
        font = (ROOT / "src/activities/settings/FontDownloadActivity.cpp").read_text()
        methods = "\n".join([
            method(ota, "void OtaUpdateActivity::beginWifiSelection()"),
            method(ota, "void OtaUpdateActivity::onExit()"),
            method(font, "void FontDownloadActivity::startWifiSelection()"),
            method(font, "void FontDownloadActivity::onExit()"),
            method(font, "void FontDownloadActivity::finishAutomaticFlow("),
        ])
        harness = r'''
#include <cassert>
#include <memory>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
constexpr int WIFI_MODE_NULL = 0;
bool childAllocationOk = true, locked = false;
int launches = 0, restartRoute = -1;
struct ActivityResult { bool isCancelled = false; };
struct Activity {
  int renderer = 0, mappedInput = 0;
  bool finished = false;
  void onExit() {}
  void finish() { finished = true; }
  void requestUpdate() {}
  void onWifiSelectionComplete(bool) {}
  template<class T, class F> void startActivityForResult(T&&, F) { ++launches; }
  template<class T, class F> bool startActivityForResultWith(F) {
    if (!childAllocationOk) return false;
    ++launches;
    return true;
  }
};
struct RenderLock {
  explicit RenderLock(Activity&) { assert(!locked); locked = true; }
  ~RenderLock() { locked = false; }
};
struct WifiSelectionActivity { WifiSelectionActivity(int, int) {} };
template<class T, class... Args> auto makeUniqueNoThrow(Args... args) {
  return childAllocationOk ? std::make_unique<T>(args...) : nullptr;
}
struct {
  bool ok = true;
  int releases = 0;
  bool releaseMemoryForNetwork() { assert(locked); ++releases; return ok; }
} READING_STATS;
struct {
  int mode = WIFI_MODE_NULL, disconnects = 0;
  int getMode() { return mode; }
  void disconnect(bool) { ++disconnects; }
} WiFi;
struct { void setAutoSyncEnabled(bool) {} } halClock;
void delay(int) {}
void silentRestart() { restartRoute = 0; }
void silentRestartToReader(bool suppress = false) { restartRoute = suppress ? 2 : 1; }
void silentRestartToReaderAndPreloadChineseFont(unsigned size) {
  assert(size == 16); restartRoute = 3;
}
struct OtaUpdateActivity : Activity {
  enum class State { Ready, Failed, WifiSelection };
  State state = State::Ready;
  bool readingStatsReleased = false;
  void beginWifiSelection();
  void onExit();
};
struct FontDownloadActivity : Activity {
  enum class Purpose { Manage, ReaderAutoInstall };
  enum class ExitRoute { Home, Reader, ReaderSuppressPrompt, ReaderPreloadChineseFont };
  Purpose purpose_ = Purpose::Manage;
  ExitRoute exitRoute_ = ExitRoute::Home;
  bool readingStatsReleased_ = false;
  unsigned targetPointSize_ = 16;
  void startWifiSelection();
  void onExit();
  void finishAutomaticFlow(ExitRoute);
};
'''
        checks = r'''
int main() {
#ifdef CONFIG_IDF_TARGET_ESP32C3
  constexpr bool c3 = true;
#else
  constexpr bool c3 = false;
#endif
  // Merely viewing OTA or declining a font prompt must not save or restart.
  OtaUpdateActivity untouchedOta;
  FontDownloadActivity untouchedFont;
  untouchedOta.onExit(); untouchedFont.onExit();
  assert(READING_STATS.releases == 0 && restartRoute == -1);
  for (int saveOk = 0; saveOk != 2; ++saveOk) {
    for (int allocOk = 0; allocOk != 2; ++allocOk) {
      READING_STATS.ok = saveOk;
      childAllocationOk = allocOk;
      const bool released = c3 && saveOk;
      const bool entered = (!c3 || saveOk) && allocOk;
      READING_STATS.releases = launches = 0; restartRoute = -1;
      OtaUpdateActivity ota;
      ota.beginWifiSelection();
      assert(ota.readingStatsReleased == released);
      assert(launches == entered && READING_STATS.releases == c3);
      if (released) {
        ota.beginWifiSelection();
        assert(READING_STATS.releases == 1); // Never unload an already empty store again.
      }
      ota.onExit(); // Cancel or allocation failure before Wi-Fi enabled.
      assert(restartRoute == (released ? 0 : -1));
      for (int automatic = 0; automatic != 2; ++automatic) {
        READING_STATS.releases = launches = 0; restartRoute = -1;
        FontDownloadActivity font;
        if (automatic) font.purpose_ = FontDownloadActivity::Purpose::ReaderAutoInstall;
        font.startWifiSelection();
        assert(font.readingStatsReleased_ == released);
        assert(launches == entered && READING_STATS.releases == c3);
        assert(font.finished == !entered);
        if (!entered && automatic)
          assert(font.exitRoute_ == FontDownloadActivity::ExitRoute::ReaderSuppressPrompt);
        font.onExit();
        assert(restartRoute == (released ? (automatic && !entered ? 2 : 0) : -1));
      }
    }
  }
  // Retain all font return destinations, with Wi-Fi active or after an early cancel.
  for (int route = 0; route != 4; ++route) {
    for (int wifi = 0; wifi != 2; ++wifi) {
      FontDownloadActivity font;
      font.readingStatsReleased_ = c3;
      font.exitRoute_ = static_cast<FontDownloadActivity::ExitRoute>(route);
      WiFi.mode = wifi; WiFi.disconnects = 0; restartRoute = -1;
      font.onExit();
      assert(WiFi.disconnects == wifi);
      assert(restartRoute == (c3 || wifi || route == 3 ? route : -1));
    }
  }
  WiFi.mode = 1; restartRoute = -1;
  OtaUpdateActivity networkOta;
  networkOta.onExit();
  assert(restartRoute == 0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "lifecycle.cpp"
            cpp.write_text(harness + methods + checks)
            executable = Path(directory) / "lifecycle"
            for defines in (["-DCONFIG_IDF_TARGET_ESP32C3=1"], ["-DCONFIG_IDF_TARGET_ESP32S3=1"]):
                with self.subTest(defines=defines):
                    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                        "-std=c++20", "-Wall", "-Wextra", "-Werror", *defines,
                        str(cpp), "-o", str(executable)
                    ], check=True)
                    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
