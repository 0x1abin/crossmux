"""Compile the production SNTP startup against a deferred-stop host model."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HalClockSntpTest(unittest.TestCase):
    def test_startup_order_and_failures(self):
        source = (ROOT / "lib/hal/HalClock.cpp").read_text()
        start = source.index("bool HalClock::startSntp()")
        method = source[start:source.index("void HalClock::stopSntp()", start)]
        harness = r'''
#include <cassert>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
using esp_err_t = int;
constexpr int ESP_OK = 0, WL_CONNECTED = 1;
struct { int status() { return WL_CONNECTED; } } WiFi;
bool running = false, coreLocked = false, stopQueued = false;
int execResult = ESP_OK, initResult = ESP_OK, startResult = ESP_OK;
int execCalls = 0, stopCalls = 0, initCalls = 0, startCalls = 0;
bool esp_sntp_enabled() { return running; }
// Deliberately do not drain the queue before the next locked API call.
void esp_sntp_stop() { stopQueued = true; }
bool sntp_enabled() { assert(coreLocked); return running; }
void sntp_stop() { assert(coreLocked); running = false; ++stopCalls; }
esp_err_t esp_netif_tcpip_exec(esp_err_t (*fn)(void*), void* ctx) {
  ++execCalls;
  if (execResult != ESP_OK) return execResult;
  coreLocked = true;
  const auto result = fn(ctx);
  coreLocked = false;
  return result;
}
struct esp_sntp_config_t { bool start = true, smooth_sync = true; };
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(...) esp_sntp_config_t{}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*) {
  ++initCalls;
  // Same precondition as lwIP sntp_setoperatingmode().
  assert(!running);
  return initResult;
}
esp_err_t esp_netif_sntp_start() {
  ++startCalls;
  if (startResult == ESP_OK) running = true;
  return startResult;
}
enum class ClockSyncState { Idle, Syncing, Succeeded, Failed };
struct HalClock {
  bool _sntpInitialized = false, _useChinaServers = false;
  ClockSyncState _syncState = ClockSyncState::Idle;
  bool startSntp();
};
void reset() {
  running = coreLocked = stopQueued = false;
  execResult = initResult = startResult = ESP_OK;
  execCalls = stopCalls = initCalls = startCalls = 0;
}
'''
        checks = r'''
int main() {
  for (bool externalRunning : {true, false}) {
    reset();
    running = externalRunning;
    HalClock clock;
    assert(clock.startSntp());
    assert(running && !stopQueued);
    assert(execCalls == 1 && stopCalls == int(externalRunning));
    assert(initCalls == 1 && startCalls == 1);
    assert(clock._sntpInitialized && clock._syncState == ClockSyncState::Syncing);
    assert(clock.startSntp());
    assert(execCalls == 1 && initCalls == 1 && startCalls == 1);
  }
  reset();
  running = true;
  execResult = -1;
  HalClock failed;
  assert(!failed.startSntp());
  assert(running && stopCalls == 0 && initCalls == 0 && startCalls == 0);
  assert(!failed._sntpInitialized && failed._syncState == ClockSyncState::Failed);
  execResult = ESP_OK;
  assert(failed.startSntp());
  assert(stopCalls == 1 && initCalls == 1 && startCalls == 1);

  reset();
  initResult = -1;
  HalClock initFailed;
  assert(!initFailed.startSntp());
  assert(!initFailed._sntpInitialized && initFailed._syncState == ClockSyncState::Failed);
  assert(startCalls == 0);

  reset();
  startResult = -1;
  HalClock startFailed;
  assert(!startFailed.startSntp());
  assert(startFailed._sntpInitialized && startFailed._syncState == ClockSyncState::Failed);
  startResult = ESP_OK;
  assert(startFailed.startSntp());
  assert(execCalls == 1 && initCalls == 1 && startCalls == 2);
}
'''
        with tempfile.TemporaryDirectory(prefix="hal-clock-sntp-") as directory:
            cpp = Path(directory) / "check.cpp"
            binary = Path(directory) / "check"
            cpp.write_text("#include <initializer_list>\n" + harness + method + checks)
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror",
                str(cpp), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
