#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalGPIO.h"
#include "Waveshare397Power.h"

#if FREEINK_DEVICE_PAPERMONO
#include <M5Pm1.h>
#endif

HalPowerManager powerManager;  // Singleton instance

// GPIO13 is the existing Xteink C3 deep-sleep shutdown signal; the X3 profile
// also identifies it as the SD power rail. Its X4 hardware role is unverified.
// Other boards use it for unrelated signals, including X4 Pro display CS.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;

namespace {
struct StandbyRetention {
  int8_t pin;
  int activeLevel;
};

StandbyRetention x3StandbyRetention() {
  const auto& board = BoardConfig::ACTIVE;
  switch (board.board) {
    case BoardConfig::Board::XteinkX3:
    case BoardConfig::Board::XteinkX3Uc8279:
      return {board.sd.powerEnable, board.sd.powerActiveHigh ? HIGH : LOW};
    default:
      // X4 failed hardware validation; all other profiles remain unvalidated.
      return {BoardConfig::PIN_UNASSIGNED, LOW};
  }
}
}  // namespace

void HalPowerManager::begin() {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  if (!Waveshare397Power::begin()) LOG_ERR("PWR", "AXP2101 initialization failed");
#endif
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
#if FREEINK_DEVICE_MURPHY_M4
  // Hardware validation found FT6336U touch unreliable after runtime CPU clock
  // changes. The main loop still uses its 50 ms idle delay on this target.
  (void)enabled;
  return;
#else
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
#endif
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  Waveshare397Power::waitForPowerButtonRelease();
#endif
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // Keep the existing GPIO13 deep-sleep shutdown behavior unchanged while
    // its exact X4 hardware role remains unverified (the SDK still handles wake).
    // Release any surviving pad hold first: hold_en survives deep sleep via
    // the SDK's deepSleep() (esp_sleep_config_gpio_isolate +
    // gpio_deep_sleep_hold_en), and a held pad silently ignores the drive.
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  gpio.prepareForDeepSleep();
  freeink::PowerManager::powerDownRailsForSleep();

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  if (Waveshare397Power::shutdown()) {
    delay(500);  // Battery power normally disappears before this returns.
  } else {
    LOG_ERR("PWR", "AXP2101 shutdown failed; falling back to ESP deep sleep");
  }
  // A failed PMIC shutdown still needs a usable wake source. GPIO5 is the
  // confirm key at runtime; it is also the fallback when USB keeps the MCU on.
  constexpr int8_t FALLBACK_WAKE_PIN = 5;
  pinMode(FALLBACK_WAKE_PIN, INPUT_PULLUP);
  while (digitalRead(FALLBACK_WAKE_PIN) == LOW) delay(50);
  freeink::PowerManager::armWakeOnPins(1ULL << FALLBACK_WAKE_PIN, true);
  freeink::PowerManager::deepSleep();
#elif FREEINK_DEVICE_PAPERMONO
  // Its power button is behind the M5PM1 PMIC rather than an ESP GPIO, so
  // normal GPIO deep sleep would have no wake source. Ask the PMIC to shut the
  // device down; a button click then restarts it through a cold boot.
  if (freeink::m5pm1::requestShutdown()) {
    delay(1000);  // allow the PMIC firmware time to drop power
  }
#endif

#if !FREEINK_DEVICE_WAVESHARE_EPAPER_397
  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
#endif
}

bool HalPowerManager::canStandbyLightSleep(const HalGPIO& gpio) const {
  return gpio.isXteinkDevice() && BoardConfig::ACTIVE.input.power >= 0 && x3StandbyRetention().pin >= 0;
}

HalPowerManager::LightSleepWakeReason HalPowerManager::lightSleepFor(const uint32_t seconds) const {
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  const StandbyRetention retention = x3StandbyRetention();
  if (seconds == 0 || powerPin < 0 || retention.pin < 0) {
    LOG_ERR("PWR", "Invalid light-sleep request: seconds=%u powerPin=%d retentionPin=%d",
            static_cast<unsigned>(seconds), powerPin, retention.pin);
    return LightSleepWakeReason::Failed;
  }

  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(powerPin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  if (digitalRead(powerPin) == (activeHigh ? HIGH : LOW)) {
    freeink::PowerManager::waitForPowerButtonRelease();
    return LightSleepWakeReason::PowerButton;
  }

  const gpio_num_t retentionPin = static_cast<gpio_num_t>(retention.pin);
  const auto cleanupLightSleep = [&] {
    esp_err_t cleanupError = gpio_wakeup_disable(static_cast<gpio_num_t>(powerPin));
    const auto keepFirstError = [&](const esp_err_t current) {
      if (cleanupError == ESP_OK && current != ESP_OK) cleanupError = current;
    };
    keepFirstError(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO));
    keepFirstError(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER));
    keepFirstError(gpio_sleep_sel_en(retentionPin));
    return cleanupError;
  };

  // ESP32-C3 isolates ordinary GPIOs in light sleep. Keep the X3 SD rail
  // actively driven instead of allowing GPIO13 to float.
  esp_err_t error = gpio_set_level(retentionPin, retention.activeLevel);
  if (error == ESP_OK) error = gpio_set_direction(retentionPin, GPIO_MODE_OUTPUT);
  if (error == ESP_OK) error = gpio_sleep_sel_dis(retentionPin);
  if (error == ESP_OK)
    error =
        gpio_wakeup_enable(static_cast<gpio_num_t>(powerPin), activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  if (error == ESP_OK) error = esp_sleep_enable_gpio_wakeup();
  if (error == ESP_OK) error = esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  if (error != ESP_OK) {
    const esp_err_t cleanupError = cleanupLightSleep();
    LOG_ERR("PWR", "Failed to configure light sleep: %d", static_cast<int>(error));
    if (cleanupError != ESP_OK) LOG_ERR("PWR", "Failed to clean up light sleep: %d", static_cast<int>(cleanupError));
    return LightSleepWakeReason::Failed;
  }

  error = esp_light_sleep_start();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const esp_err_t cleanupError = cleanupLightSleep();
  if (cause == ESP_SLEEP_WAKEUP_GPIO) freeink::PowerManager::waitForPowerButtonRelease();
  if (error != ESP_OK) {
    LOG_ERR("PWR", "Light sleep failed: %d", static_cast<int>(error));
    return LightSleepWakeReason::Failed;
  }
  if (cleanupError != ESP_OK) {
    LOG_ERR("PWR", "Failed to clean up light sleep: %d", static_cast<int>(cleanupError));
    return LightSleepWakeReason::Failed;
  }

  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return LightSleepWakeReason::Timer;
    case ESP_SLEEP_WAKEUP_GPIO:
      return LightSleepWakeReason::PowerButton;
    default:
      LOG_ERR("PWR", "Unexpected light-sleep wake cause: %d", static_cast<int>(cause));
      return LightSleepWakeReason::Failed;
  }
}

uint16_t HalPowerManager::getBatteryPercentage() const {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  const unsigned long now = millis();
  if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) return _batteryCachedPercent;
  _batteryLastPollMs = now;
  uint16_t percent = 0;
  if (Waveshare397Power::readBatteryPercentage(percent)) _batteryCachedPercent = percent;
  return _batteryCachedPercent;
#endif

  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
