#pragma once
#include <cstdint>
#include <deque>

#include "activities/RenderLock.h"
namespace freeink {
enum class SpecialKey : uint8_t {
  None,
  Enter,
  Backspace,
  Tab,
  Escape,
  Delete,
  Left,
  Right,
  Up,
  Down,
  Home,
  End,
  PageUp,
  PageDown
};
struct KeyEvent {
  SpecialKey special = SpecialKey::None;
  uint8_t keycode = 0;
};
}  // namespace freeink
struct TestBleHost {
  struct Device {
    const char* name = "remote";
    const char* addr = "00:11:22:33:44:55";
  } remote;
  int connects = 0;
  bool scanning = false, connectResult = true, startUnderLock = false;
  bool connect(const char*) {
    ++connects;
    return connectResult;
  }
  void disconnect() {}
  bool isScanning() const { return scanning; }
  void startScan(uint32_t) { scanning = true; }
  void stopScan() { scanning = false; }
  uint8_t deviceCount() const { return 1; }
  uint8_t pairedCount() const { return 1; }
  const Device& device(uint8_t) const { return remote; }
  const Device& paired(uint8_t) const { return remote; }
  const char* connectedName() const { return remote.name; }
  void forget(const char*) {}
  void releaseScanResults() {}
  bool takeConnectFailure(char*, size_t) { return false; }
  std::deque<freeink::KeyEvent> events;
  void poll() {}
  bool popKey(freeink::KeyEvent& event) {
    if (events.empty()) return false;
    event = events.front();
    events.pop_front();
    return true;
  }
  bool running = false;
  bool beginResult = true;
  int begins = 0, ends = 0;
  bool isRunning() const { return running; }
  bool isConnected() const { return running; }
  bool begin(const char*) {
    startUnderLock = renderLockDepth == 1;
    ++begins;
    running = beginResult;
    return beginResult;
  }
  void end() {
    ++ends;
    running = false;
  }
};
inline TestBleHost BleHid;
