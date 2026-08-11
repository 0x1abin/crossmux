#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  // Counting semaphore (max=1) instead of a priority-inheriting mutex: the render
  // task (core 1) and main task (core 0) both take HalPowerManager::Lock, and
  // hand the mutex across cores. ESP-IDF SMP's priority-inheritance give path
  // trips the xTaskPriorityDisinherit assert; a counting semaphore has no such
  // path, so cross-core take/give is safe.
  modeMutex = xSemaphoreCreateCounting(1, 1);
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
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
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

  // Cut the gated peripheral rails (touch/SD/EPD enables) that the template
  // display/input init may have asserted, so they don't drain the battery
  // through "off". Wake is the power button. Must run after display.deepSleep()
  // so the panel controller gets its deep-sleep command while its rail is still
  // up (enterDeepSleep() in main.cpp guarantees that ordering).
  gpio.prepareForDeepSleep();

  // Wait for the power button to be physically released (so holding it doesn't
  // immediately re-wake the device once the wake source is armed below).
  const unsigned long releaseDeadline = millis() + 1000;
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER) && millis() < releaseDeadline) {
    delay(10);
    gpio.update();
  }

  // Keep the board's power-latch pin(s) asserted across deep sleep so the
  // device stays powered on battery (EEGO A4: GPIO4). Drive each latch HIGH and
  // hold it, then latch the holds across the reset that deep sleep entails.
  BoardConfig::holdPowerRails();
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin >= 0) {
      digitalWrite(pin, HIGH);
      gpio_hold_en(static_cast<gpio_num_t>(pin));
    }
  }
  gpio_deep_sleep_hold_en();

  // Wake on the power button. EEGO A4's power key is active-high (GPIO8).
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  if (powerPin >= 0) {
    esp_sleep_enable_ext1_wakeup(1ULL << powerPin, ESP_EXT1_WAKEUP_ANY_HIGH);
  }

  esp_deep_sleep_start();
  // esp_deep_sleep_start() does not return; keep the C++ contract explicit if a
  // future SDK supplies a test stub.
  for (;;) delay(1000);
}

uint16_t HalPowerManager::getBatteryPercentage() const {
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
