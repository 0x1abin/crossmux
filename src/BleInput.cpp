#include "BleInput.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#if FREEINK_CAP_BLE_HID_HOST
#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include "SdCardFontSystem.h"
#endif

#if FREEINK_CAP_BLE_HID_HOST && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if CROSSPOINT_BLE_HOST_PSRAM
#include <esp_memory_utils.h>
#include <nimble/esp_port/port/include/esp_nimble_mem.h>
#endif
#endif

namespace bleinput {
namespace {

#if FREEINK_CAP_BLE_HID_HOST
struct MemorySnapshot {
  size_t freeInternal;
  size_t largestInternal;
  size_t minInternal;
  size_t freePsram;
  size_t largestPsram;
  size_t minPsram;
  size_t totalPsram;
};

MemorySnapshot readMemory() {
#if !defined(SIMULATOR)
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(caps),
          heap_caps_get_largest_free_block(caps),
          heap_caps_get_minimum_free_size(caps),
          heap_caps_get_free_size(psramCaps),
          heap_caps_get_largest_free_block(psramCaps),
          heap_caps_get_minimum_free_size(psramCaps),
          heap_caps_get_total_size(psramCaps)};
#else
  return {};
#endif
}
#endif

const char* specialName(const uint8_t value) {
  switch (static_cast<freeink::SpecialKey>(value)) {
    case freeink::SpecialKey::Enter:
      return tr(STR_BT_KEY_ENTER);
    case freeink::SpecialKey::Backspace:
      return tr(STR_BT_KEY_BACKSPACE);
    case freeink::SpecialKey::Tab:
      return tr(STR_BT_KEY_TAB);
    case freeink::SpecialKey::Escape:
      return tr(STR_BT_KEY_ESCAPE);
    case freeink::SpecialKey::Delete:
      return tr(STR_BT_KEY_DELETE);
    case freeink::SpecialKey::Left:
      return tr(STR_DIR_LEFT);
    case freeink::SpecialKey::Right:
      return tr(STR_DIR_RIGHT);
    case freeink::SpecialKey::Up:
      return tr(STR_DIR_UP);
    case freeink::SpecialKey::Down:
      return tr(STR_DIR_DOWN);
    case freeink::SpecialKey::Home:
      return tr(STR_BT_KEY_HOME);
    case freeink::SpecialKey::End:
      return tr(STR_BT_KEY_END);
    case freeink::SpecialKey::PageUp:
      return tr(STR_BT_KEY_PAGE_UP);
    case freeink::SpecialKey::PageDown:
      return tr(STR_BT_KEY_PAGE_DOWN);
    case freeink::SpecialKey::None:
      return nullptr;
  }
  return nullptr;
}

#if FREEINK_CAP_BLE_HID_HOST
void logMemory(const char* phase, const MemorySnapshot& memory) {
#if CROSSPOINT_BLE_HOST_PSRAM
  constexpr const char* mode = "psram";
#else
  constexpr const char* mode = "internal";
#endif
  LOG_INF("BLE", "%s: mode=%s internal free/min/max=%u/%u/%u psram total/free/min/max=%u/%u/%u/%u", phase, mode,
          static_cast<unsigned>(memory.freeInternal), static_cast<unsigned>(memory.minInternal),
          static_cast<unsigned>(memory.largestInternal), static_cast<unsigned>(memory.totalPsram),
          static_cast<unsigned>(memory.freePsram), static_cast<unsigned>(memory.minPsram),
          static_cast<unsigned>(memory.largestPsram));
#if !defined(SIMULATOR)
  LOG_INF("BLE", "stack remaining: current=%u", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
#if INCLUDE_xTaskGetHandle
  for (const char* name : {"ipc0", "ipc1", "nimble_host", "ble-conn", "ActivityManagerRender"}) {
    // FreeRTOS truncates registered task names to this same fixed length.
    char taskName[configMAX_TASK_NAME_LEN];
    snprintf(taskName, sizeof(taskName), "%s", name);
    if (const auto task = xTaskGetHandle(taskName)) {
      LOG_INF("BLE", "stack remaining: %s=%u", taskName, static_cast<unsigned>(uxTaskGetStackHighWaterMark(task)));
    }
  }
#endif
#endif
}

bool passesGate(const MemorySnapshot& memory, const StartContext context) {
  const size_t minFree = context == StartContext::Reader ? kReaderMinFreeInternal : kExplicitMinFreeInternal;
  const size_t minLargest = context == StartContext::Reader ? kReaderMinLargestInternal : kExplicitMinLargestInternal;
  LOG_INF("BLE", "start gate: internal=%u largest=%u required=%u/%u", static_cast<unsigned>(memory.freeInternal),
          static_cast<unsigned>(memory.largestInternal), static_cast<unsigned>(minFree),
          static_cast<unsigned>(minLargest));
  if (memory.freeInternal < minFree || memory.largestInternal < minLargest) return false;
#if CROSSPOINT_BLE_HOST_PSRAM && !defined(SIMULATOR)
  if (memory.freePsram < kMinFreePsram || memory.largestPsram < kMinLargestPsram) {
    LOG_ERR("BLE", "PSRAM gate failed: required=%u/%u", static_cast<unsigned>(kMinFreePsram),
            static_cast<unsigned>(kMinLargestPsram));
    return false;
  }
#endif
  return true;
}
#endif

}  // namespace

void logDiagnostics(const char* phase) {
#if FREEINK_CAP_BLE_HID_HOST
  logMemory(phase, readMemory());
#else
  (void)phase;
#endif
}

StartResult ensureStarted(GfxRenderer& renderer, const StartContext context) {
#if !FREEINK_CAP_BLE_HID_HOST
  (void)renderer;
  (void)context;
  return StartResult::Unavailable;
#else
  if (BleHid.isRunning()) return StartResult::AlreadyRunning;

  auto memory = readMemory();
  logMemory("before start", memory);
  if (!passesGate(memory, context)) {
    switch (context) {
      case StartContext::Reader:
        // Keep reading fonts registered; only discard rebuildable caches.
        if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
        break;
      case StartContext::Explicit:
        sdFontSystem.releaseLoadedFont(renderer);
        if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
        break;
    }
    memory = readMemory();
    logMemory("after memory recovery", memory);
    if (!passesGate(memory, context)) return StartResult::LowMemory;
  }

#if CROSSPOINT_BLE_HOST_PSRAM && !defined(SIMULATOR)
  // A stack buffer cannot verify the linked NimBLE allocator. Its C API owns
  // this 16-byte diagnostic allocation, which is released before starting BLE.
  void* probe = nimble_platform_mem_malloc(16);
  if (!probe) {
    LOG_ERR("BLE", "NimBLE allocator probe failed (16 bytes)");
    return StartResult::Failed;
  }
  const bool external = esp_ptr_external_ram(probe);
  nimble_platform_mem_free(probe);
  if (!external) {
    LOG_ERR("BLE", "NimBLE allocator probe returned internal RAM; refusing start");
    return StartResult::Failed;
  }
  LOG_INF("BLE", "NimBLE allocator probe: PSRAM verified (16 bytes, released)");
#endif

  HalPowerManager::Lock powerLock;
  if (!BleHid.begin("CrossMux")) {
    LOG_ERR("BLE", "BLE HID host start failed");
    logDiagnostics("start failed");
    return StartResult::Failed;
  }
  LOG_INF("BLE", "BLE HID host started");
  logDiagnostics("after start");
  return StartResult::Started;
#endif
}

void stop() {
#if FREEINK_CAP_BLE_HID_HOST
  if (!BleHid.isRunning()) return;
  HalPowerManager::Lock powerLock;
  logDiagnostics("before stop");
  BleHid.end();
  logDiagnostics("after stop");
#endif
}

bool encodeKey(const freeink::KeyEvent& event, uint8_t& kind, uint8_t& value) {
  if (event.special != freeink::SpecialKey::None) {
    kind = 0;
    value = static_cast<uint8_t>(event.special);
    return true;
  }
  if (event.keycode == 0) return false;
  kind = 1;
  value = event.keycode;
  return true;
}

void describeKey(const uint8_t kind, const uint8_t value, char* out, const size_t outLen) {
  if (!out || outLen == 0) return;
  if (kind == 0) {
    if (const char* name = specialName(value)) {
      strncpy(out, name, outLen - 1);
      out[outLen - 1] = '\0';
      return;
    }
  }
  snprintf(out, outLen, tr(STR_BT_KEY_CODE), static_cast<unsigned>(value));
}

}  // namespace bleinput
