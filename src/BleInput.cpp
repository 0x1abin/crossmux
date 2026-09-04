#include "BleInput.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#if FREEINK_CAP_BLE_HID_HOST && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#endif

namespace bleinput {
namespace {

#if FREEINK_CAP_BLE_HID_HOST
struct MemorySnapshot {
  size_t freeInternal;
  size_t largestInternal;
};

MemorySnapshot readMemory() {
#if !defined(SIMULATOR)
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(caps), heap_caps_get_largest_free_block(caps)};
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

}  // namespace

StartResult ensureStarted(const StartContext context) {
#if !FREEINK_CAP_BLE_HID_HOST
  (void)context;
  return StartResult::Unavailable;
#else
  if (BleHid.isRunning()) return StartResult::AlreadyRunning;

  const auto memory = readMemory();
  const size_t minFree = context == StartContext::Reader ? kReaderMinFreeInternal : kExplicitMinFreeInternal;
  const size_t minLargest = context == StartContext::Reader ? kReaderMinLargestInternal : kExplicitMinLargestInternal;
  LOG_INF("BLE", "start gate: internal=%u largest=%u required=%u/%u", static_cast<unsigned>(memory.freeInternal),
          static_cast<unsigned>(memory.largestInternal), static_cast<unsigned>(minFree),
          static_cast<unsigned>(minLargest));
  if (memory.freeInternal < minFree || memory.largestInternal < minLargest) return StartResult::LowMemory;

  HalPowerManager::Lock powerLock;
  if (!BleHid.begin("CrossMux")) {
    LOG_ERR("BLE", "BLE HID host start failed");
    return StartResult::Failed;
  }
  LOG_INF("BLE", "BLE HID host started");
  return StartResult::Started;
#endif
}

void stop() {
#if FREEINK_CAP_BLE_HID_HOST
  if (!BleHid.isRunning()) return;
  HalPowerManager::Lock powerLock;
  BleHid.end();
  const auto memory = readMemory();
  LOG_INF("BLE", "BLE HID host stopped: internal=%u largest=%u", static_cast<unsigned>(memory.freeInternal),
          static_cast<unsigned>(memory.largestInternal));
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
