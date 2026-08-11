#include "FrontlightManager.h"

#if FREEINK_CAP_FRONTLIGHT
#include <Wire.h>

namespace {
uint32_t maxDuty(uint8_t bits) { return (1u << bits) - 1u; }
}  // namespace
#endif

void FrontlightManager::begin() {
#if FREEINK_CAP_FRONTLIGHT
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  const auto& i2c = BoardConfig::ACTIVE.i2cFrontlight;
  if (i2c.controller == BoardConfig::I2cFrontlightController::Lm3630a) {
    if (i2c.sda < 0 || i2c.scl < 0 || i2c.enable < 0 || i2c.address == 0) return;
    Wire.begin(i2c.sda, i2c.scl, i2c.i2cHz);
    Wire.setTimeOut(256);
    pinMode(i2c.enable, OUTPUT);
    digitalWrite(i2c.enable, LOW);

    // The OEM firmware contains an LM3630A path, but at least one retail EEGO
    // A4 revision has no frontlight hardware populated. Probe with the recovered
    // enable sequence so an absent option stays off and is not exposed as a
    // capability merely because its driver exists in a shared firmware image.
    digitalWrite(i2c.enable, HIGH);
    delay(2);
    Wire.beginTransmission(i2c.address);
    const bool detected = Wire.endTransmission() == 0;
    digitalWrite(i2c.enable, LOW);
    if (!detected) return;

    _begun = true;
    _brightness = 0;
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

#if defined(ARDUINO) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // Arduino-ESP32 3.x LEDC API.
  ledcAttach(fl.gpio, fl.pwmFrequency, fl.pwmResolutionBits);
#else
  // Arduino-ESP32 2.x fallback.
  ledcSetup(0, fl.pwmFrequency, fl.pwmResolutionBits);
  ledcAttachPin(fl.gpio, 0);
#endif
  _begun = true;
  setBrightness(0);
#endif
}

void FrontlightManager::setBrightness(uint8_t percent) {
#if FREEINK_CAP_FRONTLIGHT
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  if (!_begun) return;
  if (percent > 100) percent = 100;
  _brightness = percent;
  if (percent > 0) _lastBrightness = percent;

  if (BoardConfig::ACTIVE.i2cFrontlight.controller == BoardConfig::I2cFrontlightController::Lm3630a) {
    applyLm3630a();
    return;
  }
  if (fl.gpio == BoardConfig::PIN_UNASSIGNED) return;

  const uint32_t full = maxDuty(fl.pwmResolutionBits);
  uint32_t duty = (static_cast<uint32_t>(percent) * full) / 100u;
  if (!fl.activeHigh) duty = full - duty;

#if defined(ARDUINO) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(fl.gpio, duty);
#else
  ledcWrite(0, duty);
#endif
#else
  (void)percent;
#endif
}

void FrontlightManager::off() { setBrightness(0); }
void FrontlightManager::on() { setBrightness(_lastBrightness); }

void FrontlightManager::setColorTemperature(uint8_t warmPercent) {
  _warmPercent = warmPercent > 100 ? 100 : warmPercent;
#if FREEINK_CAP_FRONTLIGHT
  if (_begun &&
      BoardConfig::ACTIVE.i2cFrontlight.controller == BoardConfig::I2cFrontlightController::Lm3630a) {
    applyLm3630a();
  }
#endif
}

bool FrontlightManager::lm3630aWrite(const uint8_t reg, const uint8_t value) {
#if FREEINK_CAP_FRONTLIGHT
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  Wire.beginTransmission(cfg.address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
#else
  (void)reg;
  (void)value;
  return false;
#endif
}

bool FrontlightManager::lm3630aRead(const uint8_t reg, uint8_t& value) {
#if FREEINK_CAP_FRONTLIGHT
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  Wire.beginTransmission(cfg.address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(cfg.address, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) != 1) return false;
  value = Wire.read();
  return true;
#else
  (void)reg;
  (void)value;
  return false;
#endif
}

bool FrontlightManager::lm3630aUpdate(const uint8_t reg, const uint8_t mask, const uint8_t value) {
  uint8_t current = 0;
  if (!lm3630aRead(reg, current)) return false;
  return lm3630aWrite(reg, static_cast<uint8_t>((current & ~mask) | (value & mask)));
}

bool FrontlightManager::configureLm3630a() {
#if FREEINK_CAP_FRONTLIGHT
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  digitalWrite(cfg.enable, HIGH);
  delay(2);
  Wire.beginTransmission(cfg.address);
  if (Wire.endTransmission() != 0) {
    digitalWrite(cfg.enable, LOW);
    return false;
  }

  // Exact EEGO A4 sequence recovered from HalBacklight/LM3630A in CrossLink.
  // Register meanings follow TI SNVS974B: filter, config, boost, max-current A/B,
  // control, then the two brightness banks.
  bool ok = lm3630aWrite(0x50, 0x03);
  ok = lm3630aUpdate(0x01, 0x07, 0x00) && ok;
  ok = lm3630aWrite(0x02, 0x38) && ok;
  ok = lm3630aUpdate(0x05, 0x1f, 0x10) && ok;
  ok = lm3630aUpdate(0x06, 0x1f, 0x10) && ok;
  ok = lm3630aUpdate(0x00, 0x14, 0x00) && ok;
  ok = lm3630aUpdate(0x00, 0x0b, 0x00) && ok;
  delay(2);
  ok = lm3630aWrite(0x03, 0x00) && ok;
  ok = lm3630aWrite(0x04, 0x00) && ok;
  _i2cConfigured = ok;
  if (!ok) digitalWrite(cfg.enable, LOW);
  return ok;
#else
  return false;
#endif
}

void FrontlightManager::applyLm3630a() {
#if FREEINK_CAP_FRONTLIGHT
  const auto& cfg = BoardConfig::ACTIVE.i2cFrontlight;
  if (_brightness == 0) {
    if (_i2cConfigured) {
      lm3630aWrite(0x03, 0);
      lm3630aWrite(0x04, 0);
    }
    digitalWrite(cfg.enable, LOW);
    _i2cConfigured = false;
    return;
  }
  if (!_i2cConfigured && !configureLm3630a()) return;

  const uint8_t level = static_cast<uint16_t>(_brightness) * 255 / 100;
  uint8_t warm = static_cast<uint16_t>(level) * _warmPercent / 100;
  uint8_t cool = static_cast<uint16_t>(level) * (100 - _warmPercent) / 100;
  // The IC ignores brightness codes 1..3; preserve a visible nonzero request.
  if (warm != 0 && warm < 4) warm = 4;
  if (cool != 0 && cool < 4) cool = 4;

  lm3630aUpdate(0x00, 0x80, 0x00);  // leave software sleep
  delay(2);
  lm3630aWrite(0x03, warm);
  lm3630aWrite(0x04, cool);
  lm3630aUpdate(0x00, 0x04, warm >= 4 ? 0x04 : 0x00);
  lm3630aUpdate(0x00, 0x02, cool >= 4 ? 0x02 : 0x00);
#endif
}
