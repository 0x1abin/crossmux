#include "BatteryMonitor.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <M5Pm1.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR < 5
#include <esp_adc_cal.h>
#endif

#include <algorithm>
#include <cmath>

#if FREEINK_BATTERY_I2C_GAUGE
#include <Wire.h>

// Minimal, dependency-free I2C fuel-gauge read for boards that carry one (e.g.
// LilyGo T5 S3: BQ27220 gauge + BQ25896 charger). Standard TI command registers;
// the gauge reports true battery state, so no ADC pin or divider is involved.
// Addresses/pins come from BoardConfig::ACTIVE.batteryGauge.
namespace {
constexpr uint8_t BQ27220_VOLTAGE = 0x08;          // battery voltage, mV (u16 LE)
constexpr uint8_t BQ27220_CURRENT = 0x0C;          // average current, signed mA (i16 LE)
constexpr uint8_t BQ27220_STATE_OF_CHARGE = 0x2C;  // SoC, percent (u16 LE)
constexpr uint8_t BQ25896_REG_STATUS = 0x0B;       // CHRG_STAT in bits [4:3]

// The gauge's I2C controller (Wire or Wire1) per BoardConfig. On single-bus SoCs
// (ESP32-C3, SOC_I2C_NUM == 1) Wire1 doesn't exist, so always use Wire there.
TwoWire& gaugeWire() {
#if SOC_I2C_NUM > 1
  if (BoardConfig::ACTIVE.batteryGauge.i2cBus == 1) return Wire1;
#endif
  return Wire;
}

bool g_wireReady = false;
void ensureWire() {
  if (g_wireReady) return;
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  gaugeWire().begin(g.i2cSda, g.i2cScl, g.i2cHz);
  g_wireReady = true;
}

bool readReg16(uint8_t addr, uint8_t reg, uint16_t& out) {
  if (addr == 0) return false;
  ensureWire();
  TwoWire& w = gaugeWire();
  w.beginTransmission(addr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) return false;
  const uint8_t lo = w.read();
  const uint8_t hi = w.read();
  out = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
  return true;
}

bool readReg8(uint8_t addr, uint8_t reg, uint8_t& out) {
  if (addr == 0) return false;
  ensureWire();
  TwoWire& w = gaugeWire();
  w.beginTransmission(addr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) return false;
  out = w.read();
  return true;
}

// Charging state for an I2C-gauge board, from the active board's gauge config.
// Two sources, in order of preference:
//   1. A dedicated charger IC (BQ25896): CHRG_STAT in REG0B[4:3] — 01 pre-charge
//      or 10 fast-charge means charging. Used by LilyGo T5 S3.
//   2. Gauge-native fallback (BQ27220 Current(), signed mA): current flowing INTO
//      the battery (> 0) means charging. Lets boards with a gauge but NO charger
//      IC — e.g. Xteink X3 — still report charge status. Current() is used rather
//      than the BatteryStatus DSG bit because DSG also clears during rest, so it
//      can't tell "charging" from "idle"; the current sign can.
// `known` is set false only when neither source responds (transient I2C failure or
// a board with neither gaugeAddr nor chargerAddr).
bool readGaugeCharging(bool& known) {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (g.chargerAddr != 0) {
    uint8_t status = 0;
    if (readReg8(g.chargerAddr, BQ25896_REG_STATUS, status)) {
      known = true;
      const uint8_t chrg = (status >> 3) & 0x03;
      return chrg == 0x01 || chrg == 0x02;
    }
  }
  uint16_t raw = 0;
  if (readReg16(g.gaugeAddr, BQ27220_CURRENT, raw)) {
    known = true;
    return static_cast<int16_t>(raw) > 0;
  }
  known = false;
  return false;
}
}  // namespace
#endif  // FREEINK_BATTERY_I2C_GAUGE

namespace {
constexpr uint8_t M5PM1_REG_PWR_SRC = 0x04;
constexpr uint8_t M5PM1_REG_VBAT_L = 0x22;
constexpr uint8_t M5PM1_REG_VIN_L = 0x24;
constexpr uint8_t M5PM1_REG_5VINOUT_L = 0x26;
constexpr uint16_t M5PM1_EXTERNAL_POWER_PRESENT_MV = 1000;

bool readM5Pm1Reg16(uint8_t reg, uint16_t& out) {
  uint16_t raw = 0;
  if (!freeink::m5pm1::readReg16(reg, &raw)) return false;
  out = raw & 0x0FFF;  // voltage registers carry 12 significant bits
  return true;
}
}  // namespace

BatteryMonitor::BatteryMonitor()
    : BatteryMonitor(BoardConfig::ACTIVE.batteryAdc, BoardConfig::ACTIVE.batteryDividerMultiplier,
                     BoardConfig::ACTIVE.batteryChargeStatus) {}

BatteryMonitor::BatteryMonitor(int8_t adcPin, float dividerMultiplier, int8_t chargeStatusPin)
    : _adcPin(adcPin), _dividerMultiplier(dividerMultiplier), _chargeStatusPin(chargeStatusPin) {
  if (_chargeStatusPin >= 0) {
    pinMode(_chargeStatusPin,
            BoardConfig::ACTIVE.batteryChargeActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
}

bool BatteryMonitor::hasAdcBackend() const {
  return _adcPin >= 0;
}

bool BatteryMonitor::hasGaugeBackend() const {
#if FREEINK_BATTERY_I2C_GAUGE
  return BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0;
#else
  return false;
#endif
}

bool BatteryMonitor::hasM5Pm1Backend() const {
  return BoardConfig::isM5StackPaperColor();
}

uint16_t BatteryMonitor::readPercentage() const {
#if FREEINK_BATTERY_I2C_GAUGE
  // Runtime, per active profile: gauge boards (X3, LilyGo) read SoC over I2C; ADC
  // boards (X4) in the same binary fall through to the divider path below.
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    uint16_t soc = 0;
    if (!readReg16(BoardConfig::ACTIVE.batteryGauge.gaugeAddr, BQ27220_STATE_OF_CHARGE, soc)) return 0;
    return soc > 100 ? 100 : soc;
  }
#endif
  if (hasM5Pm1Backend()) {
    Status status;
    if (readM5Pm1Status(status) && status.percentageKnown) return status.percentage;
    return 0;
  }
  if (!hasAdcBackend()) return 0;
  return percentageFromMillivolts(readMillivolts());
}

bool BatteryMonitor::readPercentageChecked(uint16_t& out) const {
#if FREEINK_BATTERY_I2C_GAUGE
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    uint16_t soc = 0;
    if (!readReg16(BoardConfig::ACTIVE.batteryGauge.gaugeAddr, BQ27220_STATE_OF_CHARGE, soc)) return false;
    out = soc > 100 ? 100 : soc;
    return true;
  }
#endif
  if (hasM5Pm1Backend()) {
    Status status;
    if (!readM5Pm1Status(status) || !status.percentageKnown) return false;
    out = status.percentage;
    return true;
  }
  if (!hasAdcBackend()) return false;
  out = percentageFromMillivolts(readMillivolts());
  return true;
}

BatteryMonitor::Status BatteryMonitor::readStatus() const {
  Status status;

#if FREEINK_BATTERY_I2C_GAUGE
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    status.supported = true;
    uint16_t soc = 0;
    if (readReg16(BoardConfig::ACTIVE.batteryGauge.gaugeAddr, BQ27220_STATE_OF_CHARGE, soc)) {
      status.percentageKnown = true;
      status.percentage = soc > 100 ? 100 : soc;
    }
    uint16_t mv = 0;
    if (readReg16(BoardConfig::ACTIVE.batteryGauge.gaugeAddr, BQ27220_VOLTAGE, mv)) {
      status.millivoltsKnown = true;
      status.millivolts = mv;
    }
    // Charging: from a dedicated charger IC when present, else the gauge's own
    // Current() sign — so gauge-only boards (X3) report it too.
    bool chargingKnown = false;
    const bool charging = readGaugeCharging(chargingKnown);
    status.chargingKnown = chargingKnown;
    status.charging = charging;
    return status;
  }
#endif

  if (hasM5Pm1Backend()) {
    readM5Pm1Status(status);
    return status;
  }

  if (hasAdcBackend()) {
    status.supported = true;
    status.millivolts = readMillivolts();
    status.millivoltsKnown = status.millivolts > 0;
    if (status.millivoltsKnown) {
      status.percentage = percentageFromMillivolts(status.millivolts);
      status.percentageKnown = true;
    }
    if (_chargeStatusPin >= 0) {
      status.chargingKnown = true;
      const int activeLevel = BoardConfig::ACTIVE.batteryChargeActiveHigh ? HIGH : LOW;
      status.charging = digitalRead(_chargeStatusPin) == activeLevel;
    }
  }
  return status;
}

uint16_t BatteryMonitor::readMillivolts() const {
#if FREEINK_BATTERY_I2C_GAUGE
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    uint16_t gaugeMv = 0;
    readReg16(BoardConfig::ACTIVE.batteryGauge.gaugeAddr, BQ27220_VOLTAGE, gaugeMv);
    return gaugeMv;  // gauge reports true battery mV (no divider)
  }
#endif
  if (hasM5Pm1Backend()) {
    Status status;
    if (readM5Pm1Status(status) && status.millivoltsKnown) return status.millivolts;
    return 0;
  }
  if (!hasAdcBackend()) return 0;
#if ESP_IDF_VERSION_MAJOR < 5
  // ESP-IDF 4.x doesn't have analogReadMilliVolts, so calibrate manually.
  const uint16_t raw = analogRead(_adcPin);
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  const uint16_t mv = esp_adc_cal_raw_to_voltage(raw, &adc_chars);
#else
  // ESP-IDF 5.x has analogReadMilliVolts
  const uint16_t mv = analogReadMilliVolts(_adcPin);
#endif

  return static_cast<uint16_t>(mv * _dividerMultiplier);
}

double BatteryMonitor::readVolts() const {
  return static_cast<double>(readMillivolts()) / 1000.0;
}

bool BatteryMonitor::isCharging() const {
#if FREEINK_BATTERY_I2C_GAUGE
  // Gauge boards: prefer a charger IC's status (BQ25896), else fall back to the
  // gauge's own Current() sign, so a board with a gauge but no charger IC (e.g.
  // X3) still reports charging. Unknown/failed reads report false.
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    bool known = false;
    const bool charging = readGaugeCharging(known);
    return known && charging;
  }
#endif
  if (hasM5Pm1Backend()) {
    Status status;
    return readM5Pm1Status(status) && status.chargingKnown && status.charging;
  }
  if (_chargeStatusPin < 0) {
    return false;
  }
  const int activeLevel = BoardConfig::ACTIVE.batteryChargeActiveHigh ? HIGH : LOW;
  return digitalRead(_chargeStatusPin) == activeLevel;
}

bool BatteryMonitor::readM5Pm1Status(Status& status) const {
  status.supported = true;
  if (!hasM5Pm1Backend()) return false;

  freeink::m5pm1::beginBus();

  uint16_t batMv = 0;
  if (readM5Pm1Reg16(M5PM1_REG_VBAT_L, batMv)) {
    status.millivoltsKnown = true;
    status.millivolts = batMv;
    status.percentageKnown = true;
    status.percentage = percentageFromMillivolts(batMv);
  }

  // External power can arrive on either rail: 5VIN (DC input) or 5VINOUT (the
  // bidirectional USB-C port on PaperColor), so a supply on either one counts.
  uint16_t vinMv = 0;
  uint16_t vinOutMv = 0;
  const bool vinKnown = readM5Pm1Reg16(M5PM1_REG_VIN_L, vinMv);
  const bool vinOutKnown = readM5Pm1Reg16(M5PM1_REG_5VINOUT_L, vinOutMv);
  if (vinKnown) status.pm1VinMv = vinMv;
  if (vinOutKnown) status.pm1VinOutMv = vinOutMv;
  uint8_t powerSource = 0;
  const bool pwrSrcKnown = freeink::m5pm1::readReg(M5PM1_REG_PWR_SRC, &powerSource);
  if (pwrSrcKnown) status.pm1PowerSource = powerSource & 0x07;
  if (vinKnown || vinOutKnown) {
    status.externalPowerKnown = true;
    status.externalPower = (vinKnown && vinMv > M5PM1_EXTERNAL_POWER_PRESENT_MV) ||
                           (vinOutKnown && vinOutMv > M5PM1_EXTERNAL_POWER_PRESENT_MV);
  } else if (pwrSrcKnown) {
    status.externalPowerKnown = true;
    status.externalPower = (powerSource & 0x07) == 0;
  }

  // M5PM1 exposes input power and battery voltage on PaperColor here. It does
  // not expose a proven separate charge-phase bit in this lightweight PM1 map,
  // so keep charging unknown instead of equating USB power with active charging.
  return status.percentageKnown || status.millivoltsKnown || status.externalPowerKnown;
}

uint16_t BatteryMonitor::percentageFromMillivolts(uint16_t millivolts) {
  double volts = millivolts / 1000.0;
  // Polynomial derived from LiPo samples
  double y = -144.9390 * volts * volts * volts +
             1655.8629 * volts * volts -
             6158.8520 * volts +
             7501.3202;

  // Clamp to [0,100] and round
  y = std::max(y, 0.0);
  y = std::min(y, 100.0);
  y = round(y);
  return static_cast<uint16_t>(y);
}
