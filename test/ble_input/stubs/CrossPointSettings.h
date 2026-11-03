#pragma once
#include <BlePageTurner.h>

#include "BleKeyMapping.h"
struct CrossPointSettings {
  enum { CLASSIC = 0, INX = 5 };
  uint8_t uiTheme = CLASSIC;
  enum { PWR_BOOKMARK = 1, PWR_SYNC = 2, PWR_READER_MENU = 3 };
  uint8_t shortPwrBtn = 0;
  enum { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, NEXT_NEXT = 3, PREV_PREV = 4 };
  enum VIBRATION { VIBRATION_TOUCH = 0, VIBRATION_TOUCH_PAGE = 1, VIBRATION_OFF = 2 };
  enum HAPTIC_INTENSITY { HAPTIC_LOW = 0, HAPTIC_MEDIUM = 1, HAPTIC_HIGH = 2 };
  uint8_t vibration = VIBRATION_OFF, hapticIntensity = HAPTIC_HIGH;
  uint8_t homeButtonTapAction = 0, homeButtonDoubleTapAction = 0, homeButtonLongPressAction = 0;
  int sideButtonLayout = PREV_NEXT;
  bool frontButtonFollowOrientation = true;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  bleinput::KeyMap bleKeyMap{};
  bool bluetoothEnabled = true;
  bleturner::Config ble;
  void saveToFile() {}
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
