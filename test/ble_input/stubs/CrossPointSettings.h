#pragma once
#include <BlePageTurner.h>

#include "BleKeyMapping.h"
struct CrossPointSettings {
  enum { CLASSIC = 0, INX = 5 };
  uint8_t uiTheme = CLASSIC;
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_TURN = 2,
    FORCE_REFRESH = 3,
    FOOTNOTES = 4,
    PWR_CONFIRM = 5,
    CREATE_CLIPPING = 6,
    PWR_BOOKMARK = 7,
    PWR_SYNC = 8,
    PWR_READER_MENU = 9,
    PWR_FRONTLIGHT = 10,
    SHORT_PWRBTN_COUNT
  };
  uint8_t shortPwrBtn = IGNORE, doublePwrBtn = IGNORE;
  enum { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, NEXT_NEXT = 3, PREV_PREV = 4 };
  enum VIBRATION { VIBRATION_TOUCH = 0, VIBRATION_TOUCH_PAGE = 1, VIBRATION_OFF = 2 };
  enum HAPTIC_INTENSITY { HAPTIC_LOW = 0, HAPTIC_MEDIUM = 1, HAPTIC_HIGH = 2 };
  uint8_t vibration = VIBRATION_OFF, hapticIntensity = HAPTIC_HIGH;
  uint8_t homeButtonTapAction = 0, homeButtonDoubleTapAction = 0, homeButtonLongPressAction = 0;
  int sideButtonLayout = PREV_NEXT;
  bool frontButtonFollowOrientation = true, sideButtonFollowOrientation = false;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  bleinput::KeyMap bleKeyMap{};
  bool bluetoothEnabled = true;
  bleturner::Config ble;
  void saveToFile() {}
};
inline CrossPointSettings testSettings;
#define SETTINGS testSettings
