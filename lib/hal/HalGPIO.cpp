#include <BatteryMonitor.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <XteinkDetect.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "Waveshare397Power.h"

#if FREEINK_DEVICE_MURPHY_M4
#include "MurphyM4BatchPreference.h"
#endif

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_WAVESHARE_EPAPER_397
#include <soc/usb_serial_jtag_reg.h>
#endif

// Global HalGPIO instance
HalGPIO gpio;

namespace X3GPIO {

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

}  // namespace X3GPIO

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3
#if FREEINK_DEVICE_MURPHY_M4
constexpr char NVS_KEY_M4_BATCH[] = "m4_batch_v3";
#endif

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
uint8_t wavesharePowerButtonHook() {
  return Waveshare397Power::powerButtonPressed() ? static_cast<uint8_t>(1u << HalGPIO::BTN_POWER) : 0;
}
#endif

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

uint8_t readNvsUChar(const char* key, const uint8_t defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) return defaultValue;
  const uint8_t value = prefs.getUChar(key, defaultValue);
  prefs.end();
  return value;
}

bool writeNvsUChar(const char* key, const uint8_t value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) return false;
  const bool written = prefs.putUChar(key, value) == sizeof(value);
  prefs.end();
  return written;
}

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  const uint8_t raw = readNvsUChar(key, static_cast<uint8_t>(defaultValue));
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) { writeNvsUChar(key, static_cast<uint8_t>(value)); }

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  // 0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Device override active: %s", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Using cached device type: %s", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: use FreeInk's canonical two-pass X3 fingerprint and persist
  // only confirmed results. Inconclusive probes deliberately remain uncached.
  uint8_t score1 = 0;
  uint8_t score2 = 0;
  const freeink::XteinkVerdict verdict = freeink::detectXteinkVerdict(&score1, &score2);
  LOG_INF("HW", "Xteink probe scores: pass1=%u pass2=%u verdict=%u", score1, score2, static_cast<unsigned>(verdict));

  if (verdict == freeink::XteinkVerdict::X3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }

  if (verdict == freeink::XteinkVerdict::X4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  return HalGPIO::DeviceType::X4;
}

#if FREEINK_DEVICE_MURPHY_M4
freeink::MurphyM4Batch loadMurphyM4Batch() {
  const uint8_t stored =
      readNvsUChar(NVS_KEY_M4_BATCH, MurphyM4BatchPreference::encode(freeink::MurphyM4Batch::Second));
  return MurphyM4BatchPreference::decode(stored);
}
#endif

}  // namespace

void HalGPIO::begin() {
#if FREEINK_MCU_C3
  _deviceType = detectDeviceTypeWithFingerprint();
  BoardConfig::selectDevice(deviceIsX3() ? BoardConfig::Board::XteinkX3 : BoardConfig::Board::XteinkX4);

  // Resolve the per-batch controller before SPI owns the display pins. FreeInk
  // checks the OEM hw_calib/screenType value first, then falls back to its
  // two-pass display-bus probe. X3's facade keys panel selection off the sibling
  // board profile, so preserve a detected UC8279 through setDisplayX3().
  freeink::applyXteinkDisplayController();
  if (deviceIsX3() && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
#else
  _deviceType = DeviceType::X4;
#endif
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  InputManager::setButtonHook(wavesharePowerButtonHook);
#endif
#if FREEINK_DEVICE_MURPHY_M4
  _murphyM4Batch = loadMurphyM4Batch();
  LOG_INF("HW", "Murphy M4 batch %u selected", _murphyM4Batch == freeink::MurphyM4Batch::First ? 1U : 2U);
  inputMgr.setMurphyM4Batch(_murphyM4Batch);
#endif
  inputMgr.begin();
}

#if FREEINK_DEVICE_MURPHY_M4
bool HalGPIO::saveMurphyM4Batch(const freeink::MurphyM4Batch batch) {
  if (writeNvsUChar(NVS_KEY_M4_BATCH, MurphyM4BatchPreference::encode(batch))) return true;
  LOG_ERR("HW", "Failed to save Murphy M4 batch");
  return false;
}
#endif

void HalGPIO::update() {
  inputMgr.update();
  const bool buttonActivity = inputMgr.wasPressed(BTN_BACK) || inputMgr.wasPressed(BTN_CONFIRM) ||
                              inputMgr.wasPressed(BTN_LEFT) || inputMgr.wasPressed(BTN_RIGHT) ||
                              inputMgr.wasPressed(BTN_UP) || inputMgr.wasPressed(BTN_DOWN);
  const InputModality previous = inputModality.load(std::memory_order_relaxed);
  const InputModality next = inputModalityAfter(previous, buttonActivity, inputMgr.wasTouchActivity());
  inputModalityChanged = next != previous;
  inputModality.store(next, std::memory_order_relaxed);
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

unsigned long HalGPIO::getPowerButtonHeldTime() const { return inputMgr.getPowerButtonHeldTime(); }

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyTapped() const { return inputMgr.wasHomeKeyTapped(); }

bool HalGPIO::wasHomeKeyLongPressed() const { return inputMgr.wasHomeKeyLongPressed(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }

bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return inputMgr.wasTouchLongPress(nx, ny); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

void HalGPIO::clearTouchTapEvent() { inputMgr.clearTouchTapEvent(); }

void HalGPIO::prepareForDeepSleep() { inputMgr.prepareForDeepSleep(); }

bool HalGPIO::restoreTouchAfterDisplayReset() { return inputMgr.reinitializeTouchAfterSharedReset(); }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Classic;
}

bool HalGPIO::isXteinkDevice() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::verifyPowerButtonWakeup() {
  // M5Paper v1.1: the classic ESP32's reset-to-setup() latency exceeds a normal
  // wheel click, so a click wake is always released before this samples and
  // verification would re-sleep on every wake. Its wheel has hard external
  // pull-ups, so the ghost-wake debounce this implements is not needed.
  if (BoardConfig::isPaperMono() || BoardConfig::isM5PaperV11() || BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }

  constexpr unsigned long POWER_WAKE_STABILITY_MS = 10;
  const bool heldAtFirstSample = inputMgr.isPowerButtonPhysicallyPressed();
  const unsigned long sampleStart = millis();
  inputMgr.update();
  while (millis() - sampleStart < POWER_WAKE_STABILITY_MS || inputMgr.isDebouncePending()) {
    delay(1);
    inputMgr.update();
  }
  return heldAtFirstSample && inputMgr.isPowerButtonPhysicallyPressed();
}

bool HalGPIO::verifyPowerButtonWakeup(const uint16_t requiredDurationMs, const bool shortPressAllowed) {
  if (BoardConfig::isX4Pro() || FREEINK_DEVICE_X4CLASSIC || BoardConfig::isPaperMono() || BoardConfig::isM5PaperV11() ||
      BoardConfig::ACTIVE.input.power < 0 || shortPressAllowed) {
    return true;
  }

  const unsigned long calibration = millis();
  const unsigned long calibratedDuration = calibration < requiredDurationMs ? requiredDurationMs - calibration : 1;
  const unsigned long start = millis();
  inputMgr.update();
  while (!inputMgr.isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    inputMgr.update();
  }
  if (!inputMgr.isPressed(BTN_POWER)) return false;

  do {
    delay(10);
    inputMgr.update();
  } while (inputMgr.isPressed(BTN_POWER) && inputMgr.getPowerButtonHeldTime() < calibratedDuration);
  return inputMgr.getPowerButtonHeldTime() >= calibratedDuration;
}

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_WAVESHARE_EPAPER_397
// X4 Pro has no confirmed VBUS GPIO. A USB data host is observable through the
// USB Serial/JTAG SOF counter; keep the last positive result across nearby polls.
static bool usbHostSofActive() {
  static uint32_t lastFrame = 0;
  static unsigned long lastAdvanceMs = 0;
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    lastFrame = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG);
    delay(3);  // A connected host advances the 1 kHz SOF counter within this window.
  }
  const uint32_t frame = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG);
  if (frame != lastFrame) {
    lastFrame = frame;
    lastAdvanceMs = millis();
    return true;
  }
  return lastAdvanceMs != 0 && millis() - lastAdvanceMs < 1500;
}
#endif

bool HalGPIO::isUsbConnected() const {
  if (deviceIsX3()) {
    // X3: infer USB/charging via BQ27220 Current() register (0x0C, signed mA).
    // Positive current means charging.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        return currentMa > 0;
      }
      delay(2);
    }
    return false;
  }
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  bool connected = false;
  if (Waveshare397Power::externalPowerConnected(connected)) return connected;
  return usbHostSofActive();
#endif
  if (BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }
#if FREEINK_DEVICE_X4PRO
  if (usbHostSofActive()) return true;
#endif
  // No digital USB-detect line (e.g. Sticky, whose PWR_IN_VOLT is an analog
  // divider): infer external power from charging state instead. BatteryMonitor
  // picks the board's best source — charger IC status, gauge Current() sign, or
  // a /STAT pin — and reports false on boards with no battery telemetry at all.
  // Caveat: charge termination at 100% reads as "not connected".
  static const BatteryMonitor battery;
  return battery.isCharging();
}

bool HalGPIO::coldBootImpliesPowerButton() const {
  // Xteink-style power topology: the power button energizes the rail until
  // firmware latches it, so a no-USB POWERON can only be a still-held button
  // boot, and plugging USB into an off device should charge-sleep, not boot.
  // Everything else boots on any cold boot: boards with no USB detection at
  // all (M5Paper v1.1, PaperColor, Murphy, de-link) would misread USB and
  // post-flash boots as battery button boots, and STAT-only boards like the
  // EEGO A4 misread them the same way once the charger terminates at 100%
  // (STAT inactive reads as "no USB").
  return isXteinkDevice() || BoardConfig::isPaperMono() || BoardConfig::isSticky();
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected &&
      coldBootImpliesPowerButton()) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
