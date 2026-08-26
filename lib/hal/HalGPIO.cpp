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
constexpr char NVS_KEY_M4_BATCH[] = "m4_batch_v1";
constexpr uint16_t M4_CHARGED_ADC_MIN = 3000;
constexpr uint32_t M4_CHARGE_SETTLE_MS = 50;
constexpr uint32_t M4_DISCHARGE_US = 2000;
constexpr uint32_t M4_RISE_TIMEOUT_US = 15000;
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

void writeNvsUChar(const char* key, const uint8_t value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) return;
  prefs.putUChar(key, value);
  prefs.end();
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
enum class NvsMurphyM4Batch : uint8_t { Unknown = 0, First = 1, Second = 2 };

NvsMurphyM4Batch readNvsMurphyM4Batch() {
  const uint8_t raw = readNvsUChar(NVS_KEY_M4_BATCH, static_cast<uint8_t>(NvsMurphyM4Batch::Unknown));
  if (raw > static_cast<uint8_t>(NvsMurphyM4Batch::Second)) return NvsMurphyM4Batch::Unknown;
  return static_cast<NvsMurphyM4Batch>(raw);
}

freeink::MurphyM4BatchProbe probeMurphyM4Batch(uint32_t* medianOut) {
  const int8_t pin = BoardConfig::ACTIVE.input.up;
  if (pin < 0) return freeink::MurphyM4BatchProbe::Inconclusive;

  analogSetPinAttenuation(static_cast<uint8_t>(pin), ADC_11db);
  const gpio_num_t gpioPin = static_cast<gpio_num_t>(pin);
  gpio_set_pull_mode(gpioPin, GPIO_FLOATING);
  gpio_set_direction(gpioPin, GPIO_MODE_INPUT);
  delay(M4_CHARGE_SETTLE_MS);

  const uint16_t chargedAdc = analogRead(pin);
  if (chargedAdc < M4_CHARGED_ADC_MIN) return freeink::MurphyM4BatchProbe::Inconclusive;

  std::array<uint32_t, freeink::MURPHY_M4_BATCH_SAMPLE_COUNT> riseTimes{};
  for (auto& riseTime : riseTimes) {
    gpio_set_level(gpioPin, 0);
    gpio_set_direction(gpioPin, GPIO_MODE_OUTPUT);
    delayMicroseconds(M4_DISCHARGE_US);
    if (analogRead(pin) > chargedAdc / 10) {
      gpio_set_direction(gpioPin, GPIO_MODE_INPUT);
      return freeink::MurphyM4BatchProbe::Inconclusive;
    }

    gpio_set_direction(gpioPin, GPIO_MODE_INPUT);
    const uint32_t startedAt = micros();
    while (analogRead(pin) < chargedAdc / 2) {
      if (micros() - startedAt > M4_RISE_TIMEOUT_US) return freeink::MurphyM4BatchProbe::Inconclusive;
    }
    riseTime = micros() - startedAt;
  }

  const uint32_t median = freeink::medianMurphyM4RiseTime(riseTimes);
  if (medianOut) *medianOut = median;
  return freeink::classifyMurphyM4RiseTime(median);
}

freeink::MurphyM4Batch detectMurphyM4Batch() {
#if defined(FREEINK_MURPHY_M4_BATCH1) && FREEINK_MURPHY_M4_BATCH1
  LOG_INF("HW", "Murphy M4 batch forced to first by build flag");
  return freeink::MurphyM4Batch::First;
#else
  switch (readNvsMurphyM4Batch()) {
    case NvsMurphyM4Batch::First:
      LOG_INF("HW", "Murphy M4 batch 1 from NVS cache");
      return freeink::MurphyM4Batch::First;
    case NvsMurphyM4Batch::Second:
      LOG_INF("HW", "Murphy M4 batch 2 from NVS cache");
      return freeink::MurphyM4Batch::Second;
    case NvsMurphyM4Batch::Unknown:
      break;
  }

  uint32_t medianUs = 0;
  switch (probeMurphyM4Batch(&medianUs)) {
    case freeink::MurphyM4BatchProbe::First:
      writeNvsUChar(NVS_KEY_M4_BATCH, static_cast<uint8_t>(NvsMurphyM4Batch::First));
      LOG_INF("HW", "Murphy M4 batch probe: first (median=%lu us)", static_cast<unsigned long>(medianUs));
      return freeink::MurphyM4Batch::First;
    case freeink::MurphyM4BatchProbe::Second:
      writeNvsUChar(NVS_KEY_M4_BATCH, static_cast<uint8_t>(NvsMurphyM4Batch::Second));
      LOG_INF("HW", "Murphy M4 batch probe: second (median=%lu us)", static_cast<unsigned long>(medianUs));
      return freeink::MurphyM4Batch::Second;
    case freeink::MurphyM4BatchProbe::Inconclusive:
      LOG_ERR("HW", "Murphy M4 batch probe inconclusive; using batch 2 fallback");
      return freeink::MurphyM4Batch::Second;
  }
  return freeink::MurphyM4Batch::Second;
#endif
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
  _murphyM4Batch = detectMurphyM4Batch();
  inputMgr.setMurphyM4Batch(_murphyM4Batch);
#endif
  inputMgr.begin();
}

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
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro;
}

bool HalGPIO::isXteinkDevice() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  // X4 Pro wakes on any power-button press; other boards retain the configured
  // hold-duration verification below.
  if (BoardConfig::isX4Pro() || BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }
#if defined(FREEINK_DEVICE_M5PAPER) && FREEINK_DEVICE_M5PAPER
  return true;
#endif
  if (shortPressAllowed) {
    // Fast path - no duration check needed
    return true;
  }
  // TODO: Intermittent edge case remains: a single tap followed by another single tap
  // can still power on the device. Tighten wake debounce/state handling here.

  // Calibrate: subtract boot time already elapsed, assuming button held since boot.
  const unsigned long calibration = millis();
  const unsigned long calibratedDuration = (calibration < requiredDurationMs) ? (requiredDurationMs - calibration) : 1;

  const auto start = millis();
  inputMgr.update();
  // inputMgr.isPressed() may take up to ~500ms to return correct state
  while (!inputMgr.isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    inputMgr.update();
  }
  if (inputMgr.isPressed(BTN_POWER)) {
    do {
      delay(10);
      inputMgr.update();
    } while (inputMgr.isPressed(BTN_POWER) && inputMgr.getPowerButtonHeldTime() < calibratedDuration);
    if (inputMgr.getPowerButtonHeldTime() < calibratedDuration) {
      return false;
    }
  } else {
    return false;
  }
  return true;
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
  if (BoardConfig::ACTIVE.usbDetect < 0) {
#if FREEINK_DEVICE_X4PRO
    return usbHostSofActive();
#else
    return false;
#endif
  }
  return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
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
