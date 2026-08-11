#pragma once

#include <cstdint>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down };
  enum class RowTouch : uint8_t { None, Down, Tap };

  RowTouch touch = RowTouch::None;
  int touchedRow = -1;
  Button pressed = Button::Back;
  Button released = Button::Up;
  bool hasPressed = false;
  bool hasReleased = false;

  RowTouch rowTouch(int& row, int, int, int, int, int, int) const {
    row = touchedRow;
    return touch;
  }
  bool wasPressed(Button button) const { return hasPressed && pressed == button; }
  bool wasReleased(Button button) const { return hasReleased && released == button; }
};
