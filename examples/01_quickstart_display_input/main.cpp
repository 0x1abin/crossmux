#include <Arduino.h>
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
FrontlightManager frontlight;
Rtc rtc;
SDCardManager& sd = SDCardManager::getInstance();
PortraitCanvas canvas(display);

void drawHome(const eego::BeginStatus& hardware) {
  canvas.useUiContentRect();
  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(3);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("EEGO A4 QUICKSTART");

  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 96);
  canvas.printf("Display: %s", hardware.displayReady ? "ready" : "failed");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 126);
  canvas.printf("Touch:  %s", hardware.touchReady ? "ready" : "failed");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 156);
  canvas.printf("SD:     %s", hardware.sdReady ? "ready" : "missing");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 186);
  canvas.printf("RTC:    %s", hardware.rtcReady ? "ready" : "missing/invalid");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 216);
  canvas.printf("Light:  %s",
                hardware.frontlightPresent ? "optional unit fitted" : "not fitted");

  canvas.drawRect(eego::UI_CONTENT_X, 278, eego::UI_CONTENT_WIDTH, 260, 0);
  canvas.setCursor(eego::UI_CONTENT_X + 12, 304);
  canvas.print("Tap: draw a marker");
  canvas.setCursor(36, 342);
  canvas.print("Up/Down: move marker");
  canvas.setCursor(36, 380);
  canvas.print("Power: clear");
  canvas.setCursor(36, 418);
  canvas.print("Front short: clear");
  canvas.setCursor(36, 456);
  canvas.print("Front long: full refresh");
  canvas.setCursor(36, 494);
  canvas.print("Serial: 115200 baud");
}

void drawMarker(const int16_t x, const int16_t y) {
  // Full-panel exception: this non-text marker intentionally visualizes touch
  // reach at the physical edges. Restore the normal UI clip immediately.
  canvas.useFullPanel();
  canvas.fillCircle(x, y, 12, 0);
  canvas.drawCircle(x, y, 18, 0);
  canvas.useUiContentRect();
}

}  // namespace

void setup() {
  // This must be the first hardware action on EEGO A4.
  eego::holdPower();
  delay(250);
  eego::beginUsbSerial();

  const eego::BeginStatus hardware =
      eego::beginStandardHardware(display, input, frontlight, rtc, sd);
  if (!hardware.displayReady) {
    Serial.println("FATAL: display framebuffer allocation failed");
    return;
  }

  Serial.printf("EEGO A4 quickstart: touch=%d sd=%d rtc=%d light=%d\n",
                hardware.touchReady, hardware.sdReady, hardware.rtcReady,
                hardware.frontlightPresent);
  drawHome(hardware);
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
}

void loop() {
  input.update();
  bool redraw = false;

  float nativeX = 0.0f;
  float nativeY = 0.0f;
  if (input.wasTouchTap(nativeX, nativeY)) {
    int16_t logicalX = 0;
    int16_t logicalY = 0;
    eego::normalizedTouchToLogical(nativeX, nativeY, logicalX, logicalY);
    Serial.printf("tap native=(%.3f,%.3f) logical=(%d,%d)\n", nativeX,
                  nativeY, logicalX, logicalY);
    drawMarker(logicalX, logicalY);
    redraw = true;
  }

  static int16_t keyboardMarkerY = 600;
  if (input.wasPressed(InputManager::BTN_UP)) {
    keyboardMarkerY = max<int16_t>(40, keyboardMarkerY - 40);
    drawMarker(canvas.width() / 2, keyboardMarkerY);
    redraw = true;
  }
  if (input.wasPressed(InputManager::BTN_DOWN)) {
    keyboardMarkerY = min<int16_t>(canvas.height() - 40, keyboardMarkerY + 40);
    drawMarker(canvas.width() / 2, keyboardMarkerY);
    redraw = true;
  }

  if (input.wasPressed(InputManager::BTN_POWER) ||
      input.wasHomeKeyShortPressed()) {
    canvas.useUiContentRect();
    canvas.clear();
    canvas.setTextColor(0);
    canvas.setTextSize(2);
    canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
    canvas.print("Cleared. Tap or press Up/Down.");
    redraw = true;
  }

  if (input.wasHomeKeyLongPressed()) {
    Serial.println("front key long press -> full refresh");
    display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
    redraw = false;
  } else if (redraw) {
    // The UC8279C driver automatically promotes every fifth Fast update to Full.
    display.displayBuffer(EInkDisplay::FAST_REFRESH, false);
  }

  delay(input.isDebouncePending() ? 2 : 8);
}
