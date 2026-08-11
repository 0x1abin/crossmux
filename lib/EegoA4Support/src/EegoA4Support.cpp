#include "EegoA4Support.h"

#include <BoardConfig.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

namespace eego {
namespace {

constexpr uint32_t USB_CDC_NORMAL_TX_TIMEOUT_MS = 50;

float clamp01(const float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

void holdConfiguredLatch(const int8_t pin) {
  if (pin < 0) return;
  digitalWrite(pin, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(pin));
}

}  // namespace

void holdPower() {
  gpio_deep_sleep_hold_dis();
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0,
                           BoardConfig::ACTIVE.power.latch1}) {
    if (pin >= 0) {
      gpio_hold_dis(static_cast<gpio_num_t>(pin));
    }
  }
  BoardConfig::holdPowerRails();
}

void beginUsbSerial(const uint32_t hostWaitMs) {
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(USB_CDC_NORMAL_TX_TIMEOUT_MS);
#endif
  Serial.setTimeout(20);
  const uint32_t startedAt = millis();
  while (!Serial && millis() - startedAt < hostWaitMs) {
    delay(10);
  }
}

void configureChargeStatus(const bool frontlightPresent) {
  // CrossLink 1.0.10 selects /STAT polarity from this same LM3630A population
  // branch. Keep BoardConfig as the single source BatteryMonitor reads.
  const bool activeHigh = frontlightPresent;
  BoardConfig::ACTIVE.batteryChargeActiveHigh = activeHigh;
  const int8_t pin = BoardConfig::ACTIVE.batteryChargeStatus;
  if (pin >= 0) {
    pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
}

BeginStatus beginStandardHardware(EInkDisplay& display, InputManager& input,
                                  FrontlightManager& frontlight, Rtc& rtc,
                                  SDCardManager& sd) {
  holdPower();

  input.begin();
  frontlight.begin();
  configureChargeStatus(frontlight.present());

  BeginStatus status;
  status.touchReady = input.hasTouch();
  status.frontlightPresent = frontlight.present();
  status.rtcReady = rtc.begin();
  status.sdReady = sd.begin();

  display.begin();
  status.displayReady = display.framebufferReady();
  return status;
}

void normalizedTouchToLogical(const float nativeX, const float nativeY,
                              int16_t& logicalX, int16_t& logicalY) {
  const float x = clamp01(nativeX);
  const float y = clamp01(nativeY);
  logicalX = static_cast<int16_t>(
      (1.0f - y) * (BoardConfig::ACTIVE.displayHeight - 1) + 0.5f);
  logicalY = static_cast<int16_t>(
      x * (BoardConfig::ACTIVE.displayWidth - 1) + 0.5f);
}

void touchPointToLogical(const InputManager::TouchPoint& point,
                         int16_t& logicalX, int16_t& logicalY) {
  const uint16_t maxNativeX = BoardConfig::ACTIVE.displayWidth - 1;
  const uint16_t maxNativeY = BoardConfig::ACTIVE.displayHeight - 1;
  const uint16_t nativeX = point.x > maxNativeX ? maxNativeX : point.x;
  const uint16_t nativeY = point.y > maxNativeY ? maxNativeY : point.y;
  logicalX = static_cast<int16_t>(maxNativeY - nativeY);
  logicalY = static_cast<int16_t>(nativeX);
}

[[noreturn]] void enterTimedDeepSleep(EInkDisplay& display,
                                      FrontlightManager& frontlight,
                                      uint32_t seconds) {
  if (seconds < 3) seconds = 3;
  if (seconds > 60) seconds = 60;

  frontlight.off();
  display.waitRefreshComplete();
  display.deepSleep();

  BoardConfig::holdPowerRails();
  holdConfiguredLatch(BoardConfig::ACTIVE.power.latch0);
  holdConfiguredLatch(BoardConfig::ACTIVE.power.latch1);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1'000'000ULL);
  delay(50);
  esp_deep_sleep_start();

  // esp_deep_sleep_start() does not return; keep the C++ contract explicit if
  // a future SDK supplies a test stub.
  for (;;) delay(1000);
}

}  // namespace eego
