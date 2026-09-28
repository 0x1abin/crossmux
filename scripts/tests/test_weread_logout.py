"""Run the real logout flow and cleanup functions with failing storage operations."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WeReadLogoutTest(unittest.TestCase):
    def test_logout_and_cleanup_failures(self):
        activity = (ROOT / "src/activities/apps/weread/webapi/WeReadActivity.cpp").read_text()
        store = (ROOT / "lib/WeReadWebApi/src/WeReadStore.cpp").read_text()
        browse = (ROOT / "lib/WeReadWebApi/src/WeReadBrowse.cpp").read_text()
        logout = activity.split("void WeReadActivity::performLogout()", 1)[1].split(
            "void WeReadActivity::selectMainTab", 1)[0]
        prompt = activity.split("void WeReadActivity::promptLogout()", 1)[1].split(
            "void WeReadActivity::promptClearCache", 1)[0]
        retry = activity.split("void WeReadActivity::handleLogoutErrorInput()", 1)[1].split(
            "void WeReadActivity::loop()", 1)[0]
        warning = activity.split("void WeReadActivity::loop()", 1)[1].split(
            "    case State::LogoutCacheWarning:", 1)[1].split("    case State::CacheCleared:", 1)[0]
        cleanup = store.split("bool clearSession()", 1)[1].split("bool clearCache()", 1)[0]
        browse_cleanup = browse.split("bool clearAllCaches()", 1)[1].split("bool clearLegacyWorkspace()", 1)[0]
        harness = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
std::vector<std::string> logs;
template<class... Args> void logError(const char*, const char* format, Args... args) {
  char message[256];
  snprintf(message, sizeof(message), format, args...);
  logs.emplace_back(message);
}
#define LOG_ERR(...) logError(__VA_ARGS__)
#define tr(key) #key
struct StorageStub {
  std::set<std::string> files, failures;
  std::vector<std::string> removals;
  bool exists(const char* path) { return files.count(path); }
  bool remove(const char* path) {
    removals.emplace_back(path);
    return !failures.count(path) && files.erase(path);
  }
  bool removeDir(const char* path) { return remove(path); }
} Storage;
namespace WeReadStore {
constexpr const char* kSessionPath = "/.crosspoint/weread/session.bin";
constexpr const char* kShelfPath = "/.crosspoint/weread/shelf.bin";
constexpr char kShelfPartPath[] = "/.crosspoint/weread/shelf.bin.part";
}
namespace WeReadBrowse { constexpr char kCacheRoot[] = "/.crosspoint/weread/browse-cache"; }
struct ActivityResult { bool isCancelled = false; };
struct MappedInputManager {
  enum class Button { Confirm, Back };
  bool confirm = false, back = false, tapped = false;
  bool wasReleased(Button button) { return button == Button::Confirm ? confirm : back; }
  bool wasScreenTapped(int&, int&) { return tapped; }
};
struct ConfirmationActivity {
  ConfirmationActivity(int&, MappedInputManager&, const char*, const char*) {}
};
template<class T, class... Args> auto makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct { int apps = 0; void goToApps() { ++apps; } } activityManager;
class WeReadActivity {
 public:
  enum class State { Home, LogoutError, LogoutCacheWarning };
  struct { int resets = 0; void reset() { ++resets; } } operation_;
  struct {
    bool open = true;
    bool isOpen() { return open; }
    void close() { open = false; }
  } shelfFile_;
  std::atomic<State> state_{State::Home};
  std::atomic<int> shelfSelected_{2};
  std::atomic<bool> shelfFrameInvalidated_{false};
  int shelfCount_ = 3, updates = 0, refreshes = 0, renderer = 0;
  MappedInputManager mappedInput;
  std::function<void(const ActivityResult&)> resultHandler;
  void requestUpdate() { ++updates; }
  bool refreshShelf() { ++refreshes; shelfFile_.open = true; return true; }
  void startActivityForResult(std::unique_ptr<ConfirmationActivity>,
                              std::function<void(const ActivityResult&)> handler) {
    resultHandler = std::move(handler);
  }
  void performLogout();
  void promptLogout();
  void handleLogoutErrorInput();
  void acknowledgeWarning();
};
void seedStorage() {
  Storage = {};
  Storage.files = {WeReadStore::kSessionPath, WeReadStore::kShelfPath,
                   WeReadStore::kShelfPartPath, WeReadBrowse::kCacheRoot,
                   "/WeRead/book.epub", "/.crosspoint/weread/disclaimer.accepted"};
  activityManager.apps = 0;
  logs.clear();
}
'''
        checks = r'''
int main() {
  seedStorage();
  WeReadActivity success;
  success.performLogout();
  assert(activityManager.apps == 1 && success.operation_.resets == 1);
  assert(!success.shelfFile_.open && success.shelfCount_ == 0);
  assert(success.shelfSelected_ == 0 && success.shelfFrameInvalidated_);
  assert(Storage.files == std::set<std::string>({"/WeRead/book.epub",
                                                "/.crosspoint/weread/disclaimer.accepted"}));
  assert(logs.empty());
  success.performLogout(); // Already deleted: logout is idempotent.
  assert(activityManager.apps == 2 && Storage.removals.size() == 4);

  seedStorage();
  Storage.failures.insert(WeReadStore::kSessionPath);
  WeReadActivity failed;
  failed.performLogout();
  assert(failed.state_ == WeReadActivity::State::LogoutError && activityManager.apps == 0);
  assert(failed.shelfFile_.open && failed.refreshes == 1 && failed.shelfCount_ == 3);
  assert(Storage.removals == std::vector<std::string>({WeReadStore::kSessionPath}));
  assert(logs.size() == 1 && logs[0].find(WeReadStore::kSessionPath) != std::string::npos);
  failed.mappedInput.back = true;
  failed.handleLogoutErrorInput();
  assert(failed.state_ == WeReadActivity::State::Home && Storage.files.size() == 6);
  failed.mappedInput = {};
  failed.mappedInput.confirm = true;
  Storage.failures.clear();
  failed.handleLogoutErrorInput();
  assert(activityManager.apps == 1 && !Storage.exists(WeReadStore::kSessionPath));

  for (const char* path : {WeReadStore::kShelfPath, WeReadStore::kShelfPartPath,
                           WeReadBrowse::kCacheRoot}) {
    seedStorage();
    Storage.failures.insert(path);
    WeReadActivity warning;
    warning.performLogout();
    assert(warning.state_ == WeReadActivity::State::LogoutCacheWarning && activityManager.apps == 0);
    assert(!Storage.exists(WeReadStore::kSessionPath) && Storage.exists(path));
    assert(Storage.removals.size() == 4); // Try every cache even when one removal fails.
    assert(warning.shelfCount_ == 0 && !warning.shelfFile_.open);
    assert(Storage.exists("/WeRead/book.epub") && Storage.exists("/.crosspoint/weread/disclaimer.accepted"));
    assert(logs.size() == 1 && logs[0].find(path) != std::string::npos);
    warning.acknowledgeWarning();
    assert(activityManager.apps == 0); // Wait for an explicit acknowledgment.
    for (int input = 0; input < 3; ++input) {
      warning.mappedInput = {};
      warning.mappedInput.confirm = input == 0;
      warning.mappedInput.back = input == 1;
      warning.mappedInput.tapped = input == 2;
      warning.acknowledgeWarning();
      assert(activityManager.apps == input + 1 && Storage.removals.size() == 4);
    }
  }

  seedStorage();
  WeReadActivity cancelled;
  cancelled.promptLogout();
  assert(cancelled.resultHandler);
  cancelled.resultHandler({true});
  assert(Storage.removals.empty() && Storage.files.size() == 6 && activityManager.apps == 0);
  assert(cancelled.operation_.resets == 0 && cancelled.shelfFile_.open);
  cancelled.promptLogout();
  cancelled.resultHandler({false});
  assert(activityManager.apps == 1 && !Storage.exists(WeReadStore::kSessionPath));
}
'''
        program = (harness + "\nnamespace WeReadStore { bool clearSession()" + cleanup + "}\n"
                   + "namespace WeReadBrowse { bool clearAllCaches()" + browse_cleanup + "}\n"
                   + "void WeReadActivity::performLogout()" + logout
                   + "void WeReadActivity::promptLogout()" + prompt
                   + "void WeReadActivity::handleLogoutErrorInput()" + retry
                   + "void WeReadActivity::acknowledgeWarning() { switch (state_.load()) {"
                   + "case State::LogoutCacheWarning:" + warning
                   + "case State::Home: case State::LogoutError: return; } }\n" + checks)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "logout.cpp"
            cpp.write_text(program)
            executable = Path(directory) / "logout"
            compiled = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(executable)
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
