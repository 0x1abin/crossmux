// The production module owns lifecycle. This adapter supplies only the SDK/HAL seams.
#include "BleRuntimeHarness.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#if defined(BLE_OVERLAY_HARNESS)
#include "MappedInputManager.h"
#endif
#include "RadioPort.h"
#include "Runtime.h"
#include "SdCardFontSystem.h"
#include "esp_heap_caps.h"
namespace {
GfxRenderer* target = nullptr;
MappedInputManager* input = nullptr;
uint32_t clockMs = 10000;
bleturner::Memo memo{};
bleturner::Heap heap() { return {freeInternal, largestInternal}; }
bool release() {
  RenderLock lock;
  target->cache.releaseSdFontCaches();
  return true;
}
bleturner::Why gate(const bool explicitStart, const bool afterStart) {
  auto result = bleinput::validateStart(explicitStart, afterStart);
  if (explicitStart && !afterStart && result != bleturner::Why::Ok) {
    RenderLock lock;
    sdFontSystem.releaseLoadedFont(*target);
    target->cache.clearCache();
    result = bleinput::validateStart(true, false);
  }
  return result;
}
bool deliver(bleturner::Action action) {
#ifdef BLE_OVERLAY_HARNESS
  if (!input) return false;
  switch (action) {
    case bleturner::Action::NextPage:
      return input->injectRemoteClick(RemoteButton::Next);
    case bleturner::Action::PrevPage:
      return input->injectRemoteClick(RemoteButton::Previous);
    case bleturner::Action::Confirm:
      return input->injectRemoteClick(RemoteButton::Confirm);
    case bleturner::Action::Back:
      return input->injectRemoteClick(RemoteButton::Back);
    case bleturner::Action::Up:
      return input->injectRemoteClick(RemoteButton::Up);
    case bleturner::Action::Down:
      return input->injectRemoteClick(RemoteButton::Down);
    case bleturner::Action::Left:
      return input->injectRemoteClick(RemoteButton::Left);
    case bleturner::Action::Right:
      return input->injectRemoteClick(RemoteButton::Right);
    default:
      return false;
  }
#else
  (void)action;
  return false;
#endif
}
bool yes() { return true; }
bool no() { return false; }
void hold(bool) {}
void log(bool, const char*, va_list) {}
void restart() {}
const bleturner::Host host{
    heap, release, deliver, yes, no, restart, hold, log, nullptr, gate, bleinput::legacyKeyAction, yes};
}  // namespace
namespace bleharness {
void reset(GfxRenderer& renderer, MappedInputManager* mapped) {
  target = &renderer;
  input = mapped;
  clockMs = 10000;
  bleturner::detail::resetForTests();
  SETTINGS.ble.enabled = 1;
  bleturner::begin(host, SETTINGS.ble);
}
#ifdef BLE_OVERLAY_HARNESS
bool frame(MappedInputManager& mapped) {
  mapped.update();
  bleturner::Scene scene{};
  scene.where = bleturner::Where::Reader;
  scene.visit = 1;
  scene.pageShown = true;
  return bleturner::tick(scene);
}
#endif
}  // namespace bleharness
namespace bleturner::port {
bool compiledIn() { return FREEINK_CAP_BLE_HID_HOST; }
bool begin() { return compiledIn() && BleHid.begin("CrossMux"); }
bool end(uint32_t) {
  if (BleHid.running) BleHid.end();
  return true;
}
bool running() { return BleHid.running; }
bool stopping() { return false; }
bool connected() { return BleHid.isConnected(); }
bool connecting() { return false; }
ConnectionActivity connectionActivity() { return {false, 0}; }
bool scanning() { return BleHid.scanning; }
void poll() { BleHid.poll(); }
bool popRaw(RawEdge&) { return false; }
bool popKey(KeyPress& out) {
  freeink::KeyEvent event;
  if (!BleHid.popKey(event)) return false;
  out = {event.keycode, event.mods, event.pressed};
  return true;
}
bool armBondedReconnect(uint8_t, const char*) { return true; }
Peer linked() { return {BleHid.remote.addr, BleHid.remote.name}; }
void scan(uint32_t ms) { ms ? BleHid.startScan(ms) : BleHid.stopScan(); }
bool connect(const char* addr) { return BleHid.connect(addr); }
void disconnect() { BleHid.disconnect(); }
void forget(const char* addr) { BleHid.forget(addr); }
bool takeConnectFailure(char* out, size_t len) { return BleHid.takeConnectFailure(out, len); }
uint8_t bondCount() { return BleHid.pairedCount(); }
Peer bond(uint8_t) { return linked(); }
uint8_t foundCount() { return BleHid.deviceCount(); }
Peer found(uint8_t) { return linked(); }
bool spawnStart() {
  detail::startTask();
  return true;
}
uint32_t nowMs() { return clockMs; }
void sleepMs(uint32_t ms) { clockMs += ms; }
Memo& restartMemo() { return memo; }
}  // namespace bleturner::port
