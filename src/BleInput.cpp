#include "BleInput.h"

#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace bleinput {

namespace {
volatile bool g_startInProgress = false;
}

// NimBLE controller init/deinit hang (interrupt WDT) if run at the 10 MHz low-power
// frequency, so force normal CPU speed around both. Centralized here so every caller
// (boot restore, settings toggle, reader toggle, sleep) is covered automatically.
bool ensureStarted() {
  g_startInProgress = true;
  HalPowerManager::Lock powerLock;
  const bool ok = BleHid.begin(kHostName);
  g_startInProgress = false;
  return ok;
}

bool startInProgress() { return g_startInProgress; }

// Full teardown (NimBLE deinit), not just a link drop, so the BLE stack's RAM is
// returned to the heap — otherwise memory-hungry work like EPUB inflate can't
// allocate even after the user turns Bluetooth off.
void stop() {
  HalPowerManager::Lock powerLock;
  BleHid.end();
}

bool encodeKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value) {
  if (ev.special != freeink::SpecialKey::None) {
    kind = 0;
    value = static_cast<uint8_t>(ev.special);
    return true;
  }
  if (ev.keycode != 0) {
    kind = 1;
    value = ev.keycode;
    return true;
  }
  return false;
}

namespace {
const char* specialName(uint8_t value) {
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
      return tr(STR_BT_KEY_LEFT);
    case freeink::SpecialKey::Right:
      return tr(STR_BT_KEY_RIGHT);
    case freeink::SpecialKey::Up:
      return tr(STR_BT_KEY_UP);
    case freeink::SpecialKey::Down:
      return tr(STR_BT_KEY_DOWN);
    case freeink::SpecialKey::Home:
      return tr(STR_BT_KEY_HOME);
    case freeink::SpecialKey::End:
      return tr(STR_BT_KEY_END);
    case freeink::SpecialKey::PageUp:
      return tr(STR_BT_KEY_PAGE_UP);
    case freeink::SpecialKey::PageDown:
      return tr(STR_BT_KEY_PAGE_DOWN);
    default:
      return nullptr;
  }
}
}  // namespace

void showConnectingUntilLinked(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!BleHid.isRunning() || BleHid.isConnected()) return;
  // drawPopup refreshes the panel itself, so draw once and let e-ink hold it while we
  // pump the host. Holds until the remote links, the user presses a button to bail, or
  // a generous timeout (a remote that slept after a disconnect needs a button to wake).
  GUI.drawPopup(renderer, tr(STR_BT_CONNECTING_POPUP));
  const unsigned long deadline = millis() + 10000;
  while (!BleHid.isConnected() && millis() < deadline) {
    BleHid.poll();
    input.update();
    if (input.wasAnyPressed()) break;
    delay(50);
  }
  // Note: the caller must redraw to clear the popup. For grayscale reader pages the
  // caller should also request a ghost-cleanup (HALF) refresh first — a plain fast/
  // partial refresh ghosts badly over the BW popup (see Activity::requestGhostCleanup).
}

void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (kind == 0) {
    const char* name = specialName(value);
    if (name) {
      strncpy(out, name, outLen - 1);
      out[outLen - 1] = '\0';
      return;
    }
  }
  // Printable ASCII usage handled as a generic key code; show the raw value.
  snprintf(out, outLen, tr(STR_BT_KEY_CODE), static_cast<unsigned>(value));
}

}  // namespace bleinput
