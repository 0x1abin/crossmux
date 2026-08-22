#pragma once

#include <cstdint>

unsigned long millis();

class MappedInputManager {
 public:
  enum class Button { NavNext, NavPrevious };

  bool wasPressed(const Button button) const { return pressed && button == active; }
  bool wasReleased(const Button button) const { return released && button == active; }
  bool isPressed(const Button button) const { return held && button == active; }
  unsigned long getHeldTime() const { return heldMs; }

  Button active = Button::NavNext;
  bool pressed = false;
  bool released = false;
  bool held = false;
  unsigned long heldMs = 0;
};
