#pragma once
#include "BleKeyMapping.h"
struct CrossPointSettings {
  enum { CLASSIC = 0, INX = 5 };
  uint8_t uiTheme = CLASSIC;
  enum { PREV_NEXT, NEXT_PREV, PREV_PREV, NEXT_NEXT, SIDE_BUTTONS_DISABLED };
  uint8_t homeButtonTapAction = 0, homeButtonDoubleTapAction = 0, homeButtonLongPressAction = 0;
  int sideButtonLayout = PREV_NEXT;
  bool frontButtonFollowOrientation = true;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  bleinput::KeyMap bleKeyMap{};
  bool bluetoothEnabled = true;
  void saveToFile() {}
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
