#include "Waveshare397Power.h"

#include <BoardConfig.h>

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397

#include <Logging.h>
#include <Wire.h>

namespace Waveshare397Power {
namespace {

constexpr uint8_t I2C_ADDRESS = 0x34;
constexpr int8_t IRQ_PIN = 38;

enum class Register : uint8_t {
  Status1 = 0x00,
  Status2 = 0x01,
  ChipId = 0x03,
  Common = 0x10,
  VbusVoltage = 0x15,
  VbusCurrent = 0x16,
  ChargeGauge = 0x18,
  LowBattery = 0x1A,
  Voff = 0x24,
  Key = 0x27,
  Adc = 0x30,
  InterruptEnable1 = 0x40,
  InterruptEnable2 = 0x41,
  InterruptEnable3 = 0x42,
  InterruptStatus1 = 0x48,
  InterruptStatus2 = 0x49,
  InterruptStatus3 = 0x4A,
  Precharge = 0x61,
  ChargeCurrent = 0x62,
  Termination = 0x63,
  TargetVoltage = 0x64,
  BatteryDetect = 0x68,
  ChargeLed = 0x69,
  Backup = 0x6A,
  DcOn = 0x80,
  Dc1Voltage = 0x82,
  LdoOn = 0x90,
  Aldo1Voltage = 0x92,
  Aldo2Voltage = 0x93,
  Aldo3Voltage = 0x94,
  GaugeControl = 0xA2,
  Percent = 0xA4,
};

constexpr uint8_t CHIP_ID = 0x4A;
constexpr uint8_t ALDO_AUDIO = 0x03;
constexpr uint8_t ALDO_DISPLAY = 0x04;
constexpr uint8_t PKEY_TIMING_1S_ON_4S_OFF = 0x02;
constexpr uint8_t PKEY_IRQ_MASK = 0x0F;
constexpr uint8_t PKEY_EDGE_IRQS = PowerKeyState::PRESS_IRQ | PowerKeyState::RELEASE_IRQ;
bool ready = false;
PowerKeyState powerKeyState;

bool read(Register reg, uint8_t& value) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(static_cast<uint8_t>(reg));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(I2C_ADDRESS, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) != 1) return false;
  value = Wire.read();
  return true;
}

bool write(Register reg, uint8_t value) {
  Wire.beginTransmission(I2C_ADDRESS);
  Wire.write(static_cast<uint8_t>(reg));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool updateBits(Register reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  return read(reg, current) &&
         write(reg, static_cast<uint8_t>((current & static_cast<uint8_t>(~mask)) | (value & mask)));
}

bool configure() {
  return updateBits(Register::VbusVoltage, 0x0F, 0x06) && updateBits(Register::VbusCurrent, 0x07, 0x04) &&
         updateBits(Register::Voff, 0x07, 0x00) && updateBits(Register::Key, 0x0F, PKEY_TIMING_1S_ON_4S_OFF) &&
         updateBits(Register::Precharge, 0x03, 0x02) && updateBits(Register::ChargeCurrent, 0x1F, 0x08) &&
         updateBits(Register::Termination, 0x0F, 0x01) && updateBits(Register::TargetVoltage, 0x07, 0x03) &&
         updateBits(Register::Backup, 0x07, 0x04) && updateBits(Register::LowBattery, 0xFF, 0x55) &&
         updateBits(Register::Adc, 0x1F, 0x1D) && updateBits(Register::BatteryDetect, 0x01, 0x01) &&
         updateBits(Register::ChargeLed, 0x37, 0x05) && updateBits(Register::ChargeGauge, 0x04, 0x04) &&
         updateBits(Register::GaugeControl, 0x11, 0x01) && updateBits(Register::Dc1Voltage, 0x1F, 18) &&
         updateBits(Register::Aldo1Voltage, 0x1F, 28) && updateBits(Register::Aldo2Voltage, 0x1F, 28) &&
         updateBits(Register::Aldo3Voltage, 0x1F, 28) && updateBits(Register::LdoOn, ALDO_AUDIO | ALDO_DISPLAY, 0) &&
         updateBits(Register::DcOn, 0x01, 0x01) && write(Register::InterruptStatus1, 0xFF) &&
         write(Register::InterruptStatus2, PKEY_IRQ_MASK) && write(Register::InterruptStatus3, 0xFF) &&
         write(Register::InterruptEnable1, 0) && write(Register::InterruptEnable2, PKEY_EDGE_IRQS) &&
         write(Register::InterruptEnable3, 0);
}

}  // namespace

bool begin() {
  if (ready) return true;
  const auto& sensors = BoardConfig::ACTIVE.sensors;
  Wire.begin(sensors.i2cSda, sensors.i2cScl, sensors.i2cHz);
  pinMode(IRQ_PIN, INPUT_PULLUP);
  uint8_t chipId = 0;
  if (!read(Register::ChipId, chipId) || chipId != CHIP_ID || !configure()) return false;
  ready = true;
  uint8_t status1 = 0;
  uint8_t status2 = 0;
  uint8_t percent = 0xFF;
  if (read(Register::Status1, status1) && read(Register::Status2, status2) && read(Register::Percent, percent)) {
    LOG_INF("PWR", "AXP2101 ready: battery=%u percent=%u external=%u charging=%u", (status1 & 0x08) != 0, percent,
            (status1 & 0x20) != 0 && (status2 & 0x08) == 0, ((status2 >> 5) & 0x07) == 1);
  } else {
    LOG_INF("PWR", "AXP2101 ready");
  }
  return true;
}

bool powerButtonPressed() {
  if (!ready || digitalRead(IRQ_PIN) != LOW) return powerKeyState.apply(0);

  uint8_t status = 0;
  if (!read(Register::InterruptStatus2, status)) return powerKeyState.current();
  const uint8_t pkeyStatus = status & PKEY_IRQ_MASK;
  if (pkeyStatus != 0 && !write(Register::InterruptStatus2, pkeyStatus)) return powerKeyState.current();
  return powerKeyState.apply(pkeyStatus);
}

void waitForPowerButtonRelease() {
  while (powerButtonPressed()) delay(10);
}

bool setDisplayPower(bool enabled) {
  if (!ready && !begin()) return false;
  return updateBits(Register::LdoOn, ALDO_DISPLAY, enabled ? ALDO_DISPLAY : 0);
}

bool readBatteryPercentage(uint16_t& percent) {
  if (!ready && !begin()) return false;
  uint8_t status = 0;
  uint8_t rawPercent = 0xFF;
  if (!read(Register::Status1, status) || !(status & 0x08) || !read(Register::Percent, rawPercent) ||
      rawPercent > 100) {
    return false;
  }
  percent = rawPercent;
  return true;
}

bool externalPowerConnected(bool& connected) {
  if (!ready && !begin()) return false;
  uint8_t status1 = 0;
  uint8_t status2 = 0;
  if (!read(Register::Status1, status1) || !read(Register::Status2, status2)) return false;
  connected = (status1 & 0x20) && !(status2 & 0x08);
  return true;
}

bool shutdown() {
  if (!ready && !begin()) return false;
  const bool railsOff = updateBits(Register::LdoOn, ALDO_AUDIO | ALDO_DISPLAY, 0);
  return railsOff && updateBits(Register::Common, 0x01, 0x01);
}

}  // namespace Waveshare397Power

extern "C" void freeink_board_epd_power(bool enabled) {
  if (!Waveshare397Power::setDisplayPower(enabled)) LOG_ERR("PWR", "Failed to switch AXP2101 display rail");
}

#endif
