#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>

namespace eego {

// Result of the proven OEM/CrossLink-compatible initialization sequence.
// A missing optional frontlight is not an error. Applications decide whether
// touch, RTC, or SD are essential for their own use case.
struct BeginStatus {
  bool displayReady = false;
  bool touchReady = false;
  bool rtcReady = false;
  bool sdReady = false;
  bool frontlightPresent = false;

  bool coreReady() const { return displayReady; }
};

// Call this as the first statement in setup(). It releases a deep-sleep hold
// inherited across reset and asserts the GPIO4 power latch before any slow
// operation. Calling it more than once is harmless.
void holdPower();

// Starts native USB CDC without waiting forever for a host. Call holdPower()
// first. Opening the port from macOS may reset the ESP32-S3, so RAM-only state
// must not be treated as persistent evidence.
void beginUsbSerial(uint32_t hostWaitMs = 1500);

// GPIO11 /STAT is active-high on LM3630A-equipped boards and active-low on
// boards where the optional frontlight is absent. This function updates both
// BoardConfig (used by BatteryMonitor) and the input bias. Do not configure
// GPIO11 manually in application code.
void configureChargeStatus(bool frontlightPresent);

// Initializes peripherals in the order validated on real EEGO A4 hardware:
// power latch -> GSLX680 -> optional LM3630A -> charge polarity -> PCF8563 ->
// dedicated SD SPI -> UC8279C display. The SD-before-display order is
// intentional and protects the two SPI controller mappings.
BeginStatus beginStandardHardware(EInkDisplay& display, InputManager& input,
                                  FrontlightManager& frontlight, Rtc& rtc,
                                  SDCardManager& sd);

// InputManager reports normalized panel-native coordinates. These helpers clamp
// unexpected out-of-range input and map it to the template's 552x768 clockwise
// portrait UI frame.
void normalizedTouchToLogical(float nativeX, float nativeY, int16_t& logicalX,
                              int16_t& logicalY);
void touchPointToLogical(const InputManager::TouchPoint& point,
                         int16_t& logicalX, int16_t& logicalY);

// Safe diagnostic/product baseline: turn off the optional frontlight, wait for
// and sleep the panel, retain GPIO4 HIGH, then enter deep sleep with a mandatory
// timer wake. The interval is clamped to 3..60 seconds so an example cannot
// make the unit appear bricked. This function does not return.
[[noreturn]] void enterTimedDeepSleep(EInkDisplay& display,
                                      FrontlightManager& frontlight,
                                      uint32_t seconds);

}  // namespace eego
