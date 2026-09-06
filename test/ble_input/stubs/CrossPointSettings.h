#pragma once
#include "BleKeyMapping.h"
struct CrossPointSettings {
  enum { PREV_NEXT, NEXT_PREV, SIDE_BUTTONS_DISABLED };
  int sideButtonLayout = PREV_NEXT;
  bool frontButtonFollowOrientation = true;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  bleinput::KeyMap bleKeyMap{};
  bool bluetoothEnabled = true;
  void saveToFile() {}
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
