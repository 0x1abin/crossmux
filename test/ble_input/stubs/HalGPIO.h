#pragma once
#include <array>
#include <cstdint>
inline unsigned long testMillis = 1000;
inline unsigned long millis() { return testMillis; }
struct HalGPIO {
  enum { BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_POWER };
  std::array<bool, 7> pressed{}, released{}, held{};
  unsigned long heldTime = 0, touchHeldTime = 0;
  bool touchTap = false;
  void update() {}
  bool wasPressed(uint8_t b) const { return pressed[b]; }
  bool wasReleased(uint8_t b) const { return released[b]; }
  bool isPressed(uint8_t b) const { return held[b]; }
  bool wasAnyPressed() const {
    for (bool b : pressed)
      if (b) return true;
    return false;
  }
  bool wasAnyReleased() const {
    for (bool b : released)
      if (b) return true;
    return false;
  }
  unsigned long getHeldTime() const { return heldTime; }
  bool hasTouch() const { return true; }
  unsigned long lastTouchHeldMs() const { return touchHeldTime; }
  bool wasTouchTap(float& x, float& y) const {
    x = y = 0.5f;
    return touchTap;
  }
  bool isTouchTapCandidate(float&, float&, unsigned long&) const { return false; }
  bool wasTouchLongPress(float&, float&) const { return false; }
  void suppressTouchContact() {}
  bool isTouchHeldAt(float&, float&) const { return false; }
  bool wasTouchReleased() const { return false; }
  bool wasSwipe(float&, float&, float&, float&) const { return false; }
  bool hasHomeKey() const { return false; }
  bool wasHomeKeyTapped() const { return false; }
  bool wasHomeKeyLongPressed() const { return false; }
};
