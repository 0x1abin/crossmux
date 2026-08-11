#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EegoA4Support.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>

#include "app/DiagnosticApp.h"
#include "EegoA4Hardware.h"

namespace {

EInkDisplay display(eego::EPD_SCLK, eego::EPD_MOSI, eego::EPD_CS, eego::EPD_DC,
                    eego::EPD_RESET, eego::EPD_BUSY);
InputManager input;
BatteryMonitor battery;
FrontlightManager frontlight;
Rtc rtc;
SDCardManager& sd = SDCardManager::getInstance();
DiagnosticApp app(display, input, battery, frontlight, rtc, sd);

}  // namespace

void setup() {
  // GPIO4 is a board power latch. Assert it before any slow initialization.
  eego::holdPower();

  delay(250);
  eego::beginUsbSerial();

  Serial.printf("\nEEGO A4 hardware template %s\n", eego::TEMPLATE_VERSION);
  Serial.println("Non-destructive diagnostics; type 'help' for commands.");

  const eego::BeginStatus hardware =
      eego::beginStandardHardware(display, input, frontlight, rtc, sd);
  if (!hardware.displayReady) {
    Serial.println("FATAL: framebuffer allocation failed");
    return;
  }

  app.begin();
}

void loop() {
  app.loop();
}
