#include <Arduino.h>
#include <EegoA4Support.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>

#include "CpFontRenderer.h"
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
CpFontRenderer font(sd, canvas);

constexpr const char* FONT_PATH = "/fonts/MiSansA4/MiSansA4_14.cpfont";

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

  canvas.useUiContentRect();
  canvas.clear();
  canvas.setTextColor(0);
  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas.print("CPFONT v4 / MiSansA4 14 pt");

  if (!hardware.sdReady || !font.load(FONT_PATH)) {
    canvas.setCursor(eego::UI_CONTENT_X + 4, 100);
    canvas.print("Font load failed:");
    canvas.setCursor(eego::UI_CONTENT_X + 4, 136);
    canvas.print(font.error());
    canvas.setCursor(eego::UI_CONTENT_X + 4, 200);
    canvas.print("Required SD path:");
    canvas.setCursor(eego::UI_CONTENT_X + 4, 236);
    canvas.print(FONT_PATH);
    Serial.printf("font load failed: %s\n", font.error().c_str());
  } else {
    uint16_t missing = 0;
    uint16_t ioErrors = 0;
    font.drawText(eego::UI_CONTENT_X + 4, 100,
                  "English: The quick brown fox 0123456789",
                  &missing, &ioErrors);
    font.drawText(eego::UI_CONTENT_X + 4, 165,
                  "中文：你好，世界！这是中文字体测试。",
                  &missing, &ioErrors);
    font.drawText(eego::UI_CONTENT_X + 4, 230,
                  "混排：EEGO A4 支持 English 与中文。",
                  &missing, &ioErrors);
    font.drawText(eego::UI_CONTENT_X + 4, 295,
                  "标点：，。！？《》【】（）—…", &missing, &ioErrors);
    canvas.setTextSize(2);
    canvas.setCursor(eego::UI_CONTENT_X + 4, 390);
    canvas.printf("glyphs=%lu intervals=%lu",
                  static_cast<unsigned long>(font.info().glyphCount),
                  static_cast<unsigned long>(font.info().intervalCount));
    canvas.setCursor(eego::UI_CONTENT_X + 4, 425);
    canvas.printf("missing=%u bitmap_io_errors=%u", missing, ioErrors);
    Serial.printf(
        "font=%s glyphs=%lu intervals=%lu missing=%u bitmap_io_errors=%u\n",
        FONT_PATH, static_cast<unsigned long>(font.info().glyphCount),
        static_cast<unsigned long>(font.info().intervalCount), missing,
        ioErrors);
  }

  canvas.setTextSize(2);
  canvas.setCursor(eego::UI_CONTENT_X + 4, 510);
  canvas.print("Product UI: use CrossPoint");
  canvas.setCursor(eego::UI_CONTENT_X + 4, 545);
  canvas.print("renderer for full layout.");
  display.displayBuffer(EInkDisplay::FULL_REFRESH, false);
}

void loop() {
  // The sample page intentionally remains visible until reset.
  delay(50);
}
