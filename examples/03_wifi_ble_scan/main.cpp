#include <Arduino.h>
#include <EegoA4Support.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <NimBLEDevice.h>
#include <Rtc.h>
#include <SDCardManager.h>
#include <WiFi.h>

#include "EegoA4Hardware.h"
#include "PortraitCanvas.h"

namespace {

EInkDisplay display(eego::EPD_SCLK, eego::EPD_MOSI, eego::EPD_CS,
                    eego::EPD_DC, eego::EPD_RESET, eego::EPD_BUSY);
InputManager input;
FrontlightManager frontlight;
Rtc rtc;
SDCardManager& sd = SDCardManager::getInstance();
PortraitCanvas canvas(display);

int scanWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, false);
  delay(50);
  const int found = WiFi.scanNetworks(false, true, false, 300);
  for (int i = 0; i < found && i < 8; ++i) {
    Serial.printf("wifi[%d] rssi=%d channel=%d ssid=%s\n", i, WiFi.RSSI(i),
                  WiFi.channel(i), WiFi.SSID(i).c_str());
  }
  WiFi.scanDelete();
  WiFi.disconnect(true, false);  // eraseAP=false: do not mutate saved config
  WiFi.mode(WIFI_OFF);
  return found;
}

int scanBle() {
  NimBLEDevice::init("EEGO-A4-EXAMPLE");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(false);
  scan->setInterval(80);
  scan->setWindow(40);
  const NimBLEScanResults results = scan->getResults(5000);
  const int count = results.getCount();
  for (int i = 0; i < count && i < 8; ++i) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    if (device) {
      Serial.printf("ble[%d] rssi=%d address=%s\n", i, device->getRSSI(),
                    device->getAddress().toString().c_str());
    }
  }
  scan->clearResults();
  NimBLEDevice::deinit(true);
  return count;
}

void runScans() {
  canvas.useUiContentRect();
  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(3);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("RADIO SCAN");
  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 100);
  canvas.print("Scanning 2.4 GHz Wi-Fi...");
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);

  const int wifiCount = scanWifi();
  const int bleCount = scanBle();

  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(3);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("RADIO SCAN COMPLETE");
  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 120);
  canvas.printf("Wi-Fi networks: %d", wifiCount);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 170);
  canvas.printf("BLE advertisers: %d", bleCount);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 240);
  canvas.print("Details are on USB serial.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 290);
  canvas.print("Power button: scan again.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 360);
  canvas.print("No credentials are saved.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 410);
  canvas.print("BLE is deinitialized after scan.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 460);
  canvas.print("ESP32-S3 has no Bluetooth Classic.");
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
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
  runScans();
}

void loop() {
  input.update();
  if (input.wasPressed(InputManager::BTN_POWER)) {
    runScans();
  }
  delay(input.isDebouncePending() ? 2 : 10);
}
