#pragma once

// FreeInk SDK — frontlight manager.
//
// Drives either a PWM frontlight or an I2C LM3630A described by the active board
// profile. Inert on boards without one, so it is always safe to construct.
//
// The single brightness-PWM path drives the de-link board's primary LED (and the
// LilyGo backlight). Warm/cool color mixing and boost-driver fault/OVP sensing
// (de-link's GPIO6/7/17/18) are no-op hooks.

#include <Arduino.h>
#include <BoardConfig.h>

class FrontlightManager {
 public:
  // Bring up the PWM channel. No-op if the board has no frontlight.
  void begin();

  // Set brightness as a 0-100 percentage. 0 turns the light off.
  void setBrightness(uint8_t percent);

  // Convenience: fully off / restore last brightness.
  void off();
  void on();

  // Warm/cool mix, 0 = fully cool, 100 = fully warm.
  void setColorTemperature(uint8_t warmPercent);

  bool present() const {
#if FREEINK_CAP_FRONTLIGHT
    // A configured I2C controller may be an optional, unpopulated circuit (as
    // on the hardware-validated EEGO A4). Only report it after begin() gets an
    // actual ACK. A configured PWM GPIO is not probeable and remains profile-
    // authoritative.
    return BoardConfig::ACTIVE.frontlight.gpio != BoardConfig::PIN_UNASSIGNED ||
           (BoardConfig::hasI2cFrontlight() && _begun);
#else
    return false;  // frontlight code not compiled in (FREEINK_CAP_FRONTLIGHT=0)
#endif
  }
  uint8_t brightness() const { return _brightness; }

 private:
  bool _begun = false;
  uint8_t _brightness = 0;
  uint8_t _lastBrightness = 50;
  uint8_t _warmPercent = 50;
  bool _i2cConfigured = false;

  bool lm3630aWrite(uint8_t reg, uint8_t value);
  bool lm3630aRead(uint8_t reg, uint8_t& value);
  bool lm3630aUpdate(uint8_t reg, uint8_t mask, uint8_t value);
  bool configureLm3630a();
  void applyLm3630a();
};
