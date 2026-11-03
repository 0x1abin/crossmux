#pragma once

// One logical click per main-loop frame; never carry it into a newly opened screen.
enum class RemoteButton { None, Confirm, Back, Next, Previous, Up, Down, Left, Right };

struct RemoteButtonInput {
  RemoteButton button = RemoteButton::None;

  bool click(const RemoteButton next) {
    if (next == RemoteButton::None || button != RemoteButton::None) return false;
    button = next;
    return true;
  }
  void reset() { button = RemoteButton::None; }
};
