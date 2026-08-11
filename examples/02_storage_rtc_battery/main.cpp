#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EegoA4Support.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>

#include "EegoA4Hardware.h"
#include "PortraitCanvas.h"

namespace {

EInkDisplay display(eego::EPD_SCLK, eego::EPD_MOSI, eego::EPD_CS,
                    eego::EPD_DC, eego::EPD_RESET, eego::EPD_BUSY);
InputManager input;
BatteryMonitor battery;
FrontlightManager frontlight;
Rtc rtc;
SDCardManager& sd = SDCardManager::getInstance();
PortraitCanvas canvas(display);

constexpr const char* TEST_PATH = "/eego-a4-example.tmp";

bool testStorage() {
  if (!sd.ready()) return false;
  constexpr const char* payload = "EEGO-A4 storage example\n";
  if (!sd.writeFile(TEST_PATH, payload)) return false;
  const String readBack = sd.readFile(TEST_PATH);
  const bool removed = sd.remove(TEST_PATH);
  return readBack == payload && removed;
}

String readRtc() {
  Rtc::DateTime now;
  if (!rtc.now(now)) {
    return "invalid; set over your app/console";
  }
  char value[32];
  snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u:%02u", now.year,
           now.month, now.day, now.hour, now.minute, now.second);
  return value;
}

void printBattery(const BatteryMonitor::Status& status) {
  Serial.printf(
      "battery supported=%d mv_known=%d mv=%u percent_known=%d percent=%u "
      "charging_known=%d charging=%d raw_gpio11=%d active_high=%d\n",
      status.supported, status.millivoltsKnown, status.millivolts,
      status.percentageKnown, status.percentage, status.chargingKnown,
      status.charging, digitalRead(eego::CHARGE_STATUS),
      BoardConfig::ACTIVE.batteryChargeActiveHigh);
}

}  // namespace

void setup() {
  eego::holdPower();
  delay(250);
  eego::beginUsbSerial();

  const eego::BeginStatus hardware =
      eego::beginStandardHardware(display, input, frontlight, rtc, sd);
  if (!hardware.displayReady) {
    Serial.println("FATAL: display framebuffer allocation failed");
    return;
  }

  const bool storagePassed = testStorage();
  const String rtcValue = readRtc();
  const BatteryMonitor::Status batteryStatus = battery.readStatus();
  printBattery(batteryStatus);

  canvas.useUiContentRect();
  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(3);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("STORAGE / RTC / BATTERY");
  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 104);
  canvas.printf("SD mount: %s", hardware.sdReady ? "ready" : "missing");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 138);
  canvas.printf("SD R/W/remove: %s", storagePassed ? "PASS" : "FAIL");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 172);
  canvas.printf("Capacity: %llu MiB",
                static_cast<unsigned long long>(sd.sdTotalBytes() / 1024 / 1024));
  canvas.setCursor(eego::UI_CONTENT_X + 4, 220);
  canvas.printf("RTC: %s", rtcValue.c_str());
  canvas.setCursor(eego::UI_CONTENT_X + 4, 268);
  canvas.printf("Battery: %u mV (%u%%)", batteryStatus.millivolts,
                batteryStatus.percentage);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 302);
  canvas.printf("Charging: %s",
                batteryStatus.chargingKnown
                    ? (batteryStatus.charging ? "yes" : "no")
                    : "unknown");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 336);
  canvas.printf("GPIO11 active-%s",
                BoardConfig::ACTIVE.batteryChargeActiveHigh ? "HIGH" : "LOW");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 400);
  canvas.print("This example never formats the card,");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 432);
  canvas.print("writes NVS, or changes the RTC.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 484);
  canvas.print("Battery percentage is an estimate.");
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
}

void loop() {
  // Read-only telemetry every five seconds. Do not refresh e-paper at this rate.
  static uint32_t nextReadAt = 0;
  if (millis() >= nextReadAt) {
    nextReadAt = millis() + 5000;
    printBattery(battery.readStatus());
  }
  delay(20);
}
