#include <Arduino.h>
#include <EegoA4Support.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>
#include <esp_sleep.h>

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
bool sleepTriggered = false;

void drawInstructions() {
  canvas.useUiContentRect();
  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(3);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("SAFE DEEP SLEEP");
  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 110);
  canvas.print("Default behavior: never sleep.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 165);
  canvas.print("Hold Power for 1.5 seconds");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 198);
  canvas.print("to sleep for 10 seconds.");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 270);
  canvas.print("The helper always:");
  canvas.setCursor(42, 315);
  canvas.print("- waits for the EPD");
  canvas.setCursor(42, 350);
  canvas.print("- switches off frontlight");
  canvas.setCursor(42, 385);
  canvas.print("- keeps GPIO4 latched HIGH");
  canvas.setCursor(42, 420);
  canvas.print("- enables timer wake first");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 500);
  canvas.print("Wake restarts setup().");
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
  Serial.printf("wake cause=%d\n", static_cast<int>(esp_sleep_get_wakeup_cause()));
  drawInstructions();
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
}

void loop() {
  input.update();
  if (!sleepTriggered && input.isPressed(InputManager::BTN_POWER) &&
      input.getPowerButtonHeldTime() >= 1500) {
    sleepTriggered = true;
    canvas.useUiContentRect();
    canvas.clear();
    canvas.setTextColor(0);
    canvas.setTextSize(3);
    canvas.setCursor(eego::UI_CONTENT_X + 4, 100);
    canvas.print("SLEEPING FOR 10 SECONDS");
    canvas.setTextSize(2);
    canvas.setCursor(eego::UI_CONTENT_X + 4, 180);
    canvas.print("Timer wake is already mandatory.");
    display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
    Serial.println("entering bounded 10-second deep sleep");
    Serial.flush();
    eego::enterTimedDeepSleep(display, frontlight, 10);
  }
  delay(input.isDebouncePending() ? 2 : 10);
}
