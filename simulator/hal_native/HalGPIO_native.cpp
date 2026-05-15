// HAL GPIO backend for the desktop simulator.
//
// Maintains the 7 logical button states from HalGPIO::BTN_* and accepts keyboard
// events forwarded by simulator_main.cpp (SDL event pump).

#include <HalGPIO.h>

#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {
constexpr size_t kButtonCount = 7;

// Matches hardware InputManager edge semantics: events are valid for exactly
// one update() cycle. injectButton() (SDL thread) writes to pending* fields;
// update() (firmware thread, once per loop) latches pending → frame and clears
// pending; reads return frame* without consuming. Two-stage buffer prevents
// losing events that arrive after the firmware reads but before the next update().
struct ButtonStates {
  bool pressed = false;
  bool pendingPressed = false;
  bool pendingReleased = false;
  bool framePressed = false;
  bool frameReleased = false;
  unsigned long pressedAtMs = 0;
};

std::mutex& state_mutex() {
  static std::mutex m;
  return m;
}

ButtonStates g_buttons[kButtonCount];
}  // namespace

namespace simulator {
// Called from simulator_main.cpp on each SDL key event.
void injectButton(uint8_t buttonIndex, bool down) {
  if (buttonIndex >= kButtonCount) return;
  std::lock_guard<std::mutex> lock(state_mutex());
  ButtonStates& b = g_buttons[buttonIndex];
  if (down && !b.pressed) {
    b.pressed = true;
    b.pendingPressed = true;
    b.pressedAtMs = static_cast<unsigned long>(SDL_GetTicks());
  } else if (!down && b.pressed) {
    b.pressed = false;
    b.pendingReleased = true;
  }
}
}  // namespace simulator

void HalGPIO::begin() {}

void HalGPIO::update() {
  std::lock_guard<std::mutex> lock(state_mutex());
  for (auto& b : g_buttons) {
    b.framePressed = b.pendingPressed;
    b.frameReleased = b.pendingReleased;
    b.pendingPressed = false;
    b.pendingReleased = false;
  }
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= kButtonCount) return false;
  std::lock_guard<std::mutex> lock(state_mutex());
  return g_buttons[buttonIndex].pressed;
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= kButtonCount) return false;
  std::lock_guard<std::mutex> lock(state_mutex());
  return g_buttons[buttonIndex].framePressed;
}

bool HalGPIO::wasAnyPressed() const {
  std::lock_guard<std::mutex> lock(state_mutex());
  for (auto& b : g_buttons) {
    if (b.framePressed) return true;
  }
  return false;
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= kButtonCount) return false;
  std::lock_guard<std::mutex> lock(state_mutex());
  return g_buttons[buttonIndex].frameReleased;
}

bool HalGPIO::wasAnyReleased() const {
  std::lock_guard<std::mutex> lock(state_mutex());
  for (auto& b : g_buttons) {
    if (b.frameReleased) return true;
  }
  return false;
}

unsigned long HalGPIO::getHeldTime() const {
  std::lock_guard<std::mutex> lock(state_mutex());
  unsigned long now = static_cast<unsigned long>(SDL_GetTicks());
  unsigned long maxHeld = 0;
  for (auto& b : g_buttons) {
    if (b.pressed && b.pressedAtMs > 0) {
      unsigned long held = now - b.pressedAtMs;
      if (held > maxHeld) maxHeld = held;
    }
  }
  return maxHeld;
}

void HalGPIO::startDeepSleep() { std::exit(0); }

void HalGPIO::verifyPowerButtonWakeup(uint16_t /*requiredDurationMs*/, bool /*shortPressAllowed*/) {
  // No-op: in the simulator we always proceed past the boot wakeup gate.
}

bool HalGPIO::isUsbConnected() const { return true; }
bool HalGPIO::wasUsbStateChanged() const { return false; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::Other; }

HalGPIO gpio;
