#pragma once

#include <Arduino.h>

namespace eego {

constexpr const char* TEMPLATE_VERSION = "1.0.18";

// Keep diagnostics bounded when no host is reading, while allowing long
// status/JSON lines to drain without truncation. Full framebuffer transfer
// uses the larger timeout only for the duration of the screenshot command.
constexpr uint32_t USB_CDC_NORMAL_TX_TIMEOUT_MS = 50;
constexpr uint32_t USB_CDC_SCREENSHOT_TX_TIMEOUT_MS = 250;

// ESP32-S3 / storage
constexpr uint32_t FLASH_BYTES = 16U * 1024U * 1024U;
constexpr uint32_t PSRAM_BYTES = 8U * 1024U * 1024U;

// The UC8279C scans 768x600. Firmware supplies 552 framebuffer rows followed
// by 48 white padding rows.
constexpr uint16_t PANEL_WIDTH = 768;
constexpr uint16_t PANEL_HEIGHT = 552;
constexpr uint16_t CONTROLLER_HEIGHT = 600;
constexpr uint16_t CONTROLLER_PADDING_ROWS = 48;
constexpr uint16_t LOGICAL_WIDTH = PANEL_HEIGHT;
constexpr uint16_t LOGICAL_HEIGHT = PANEL_WIDTH;
constexpr uint16_t PANEL_ROW_BYTES = PANEL_WIDTH / 8;

// Display-safe UI geometry, established by repeated visual calibration on the
// retail EEGO A4. The physical aperture is modelled as an outer R=60 rounded
// rectangle. Foreground/UI content is inset by 12 px, so the matching inner
// rounded rectangle has R=48. Backgrounds and explicit edge/panel tests may
// use the full framebuffer, but ordinary text and controls must stay here.
constexpr int16_t DISPLAY_OUTER_RADIUS = 60;
constexpr int16_t SAFE_CONTENT_INSET = 12;
constexpr int16_t SAFE_CONTENT_RADIUS =
    DISPLAY_OUTER_RADIUS - SAFE_CONTENT_INSET;
constexpr int16_t SAFE_CONTENT_X = SAFE_CONTENT_INSET;
constexpr int16_t SAFE_CONTENT_Y = SAFE_CONTENT_INSET;
constexpr int16_t SAFE_CONTENT_WIDTH =
    static_cast<int16_t>(LOGICAL_WIDTH) - SAFE_CONTENT_INSET * 2;
constexpr int16_t SAFE_CONTENT_HEIGHT =
    static_cast<int16_t>(LOGICAL_HEIGHT) - SAFE_CONTENT_INSET * 2;
constexpr int16_t SAFE_CONTENT_RIGHT =
    SAFE_CONTENT_X + SAFE_CONTENT_WIDTH - 1;
constexpr int16_t SAFE_CONTENT_BOTTOM =
    SAFE_CONTENT_Y + SAFE_CONTENT_HEIGHT - 1;

// Ordinary foreground content uses a second, zero-radius rectangle. A square
// inside the R48 corner needs ceil(48 * (1 - 1/sqrt(2))) = 15 px additional
// inset, or 27 px from the panel edge. Use 28 px to align the layout to a 4 px
// grid and retain a small integer-raster margin.
constexpr int16_t UI_CONTENT_INSET = 28;
constexpr int16_t UI_CONTENT_X = UI_CONTENT_INSET;
constexpr int16_t UI_CONTENT_Y = UI_CONTENT_INSET;
constexpr int16_t UI_CONTENT_WIDTH =
    static_cast<int16_t>(LOGICAL_WIDTH) - UI_CONTENT_INSET * 2;
constexpr int16_t UI_CONTENT_HEIGHT =
    static_cast<int16_t>(LOGICAL_HEIGHT) - UI_CONTENT_INSET * 2;
constexpr int16_t UI_CONTENT_RIGHT =
    UI_CONTENT_X + UI_CONTENT_WIDTH - 1;
constexpr int16_t UI_CONTENT_BOTTOM =
    UI_CONTENT_Y + UI_CONTENT_HEIGHT - 1;

static_assert(SAFE_CONTENT_RADIUS == 48,
              "EEGO A4 inner safe-area radius must remain R48");
static_assert(SAFE_CONTENT_WIDTH == 528 && SAFE_CONTENT_HEIGHT == 744,
              "EEGO A4 safe-area dimensions must remain 528x744");
static_assert(UI_CONTENT_WIDTH == 496 && UI_CONTENT_HEIGHT == 712,
              "EEGO A4 rectangular UI content must remain 496x712");

// Pixel-accurate predicate for the inclusive 528x744, R48 safe-content shape.
// Circle centres match Adafruit_GFX::drawRoundRect(x, y, w, h, r).
constexpr bool isInsideSafeContent(const int16_t x, const int16_t y) {
  if (x < SAFE_CONTENT_X || x > SAFE_CONTENT_RIGHT ||
      y < SAFE_CONTENT_Y || y > SAFE_CONTENT_BOTTOM) {
    return false;
  }

  const int16_t leftCenter = SAFE_CONTENT_X + SAFE_CONTENT_RADIUS;
  const int16_t rightCenter = SAFE_CONTENT_RIGHT - SAFE_CONTENT_RADIUS;
  const int16_t topCenter = SAFE_CONTENT_Y + SAFE_CONTENT_RADIUS;
  const int16_t bottomCenter = SAFE_CONTENT_BOTTOM - SAFE_CONTENT_RADIUS;
  if ((x >= leftCenter && x <= rightCenter) ||
      (y >= topCenter && y <= bottomCenter)) {
    return true;
  }

  const int16_t centerX = x < leftCenter ? leftCenter : rightCenter;
  const int16_t centerY = y < topCenter ? topCenter : bottomCenter;
  const int32_t dx = static_cast<int32_t>(x) - centerX;
  const int32_t dy = static_cast<int32_t>(y) - centerY;
  return dx * dx + dy * dy <=
         static_cast<int32_t>(SAFE_CONTENT_RADIUS) * SAFE_CONTENT_RADIUS;
}

constexpr bool isInsideUiContentRect(const int16_t x, const int16_t y) {
  return x >= UI_CONTENT_X && x <= UI_CONTENT_RIGHT &&
         y >= UI_CONTENT_Y && y <= UI_CONTENT_BOTTOM;
}

static_assert(isInsideSafeContent(UI_CONTENT_X, UI_CONTENT_Y) &&
                  isInsideSafeContent(UI_CONTENT_RIGHT, UI_CONTENT_Y) &&
                  isInsideSafeContent(UI_CONTENT_X, UI_CONTENT_BOTTOM) &&
                  isInsideSafeContent(UI_CONTENT_RIGHT, UI_CONTENT_BOTTOM),
              "rectangular UI content must fit inside the rounded R48 region");

// UC8279C display SPI and power.
constexpr int8_t EPD_SCLK = 42;
constexpr int8_t EPD_MOSI = 45;
constexpr int8_t EPD_CS = 21;
constexpr int8_t EPD_DC = 14;
constexpr int8_t EPD_RESET = 13;
constexpr int8_t EPD_BUSY = 41;
constexpr int8_t EPD_POWER_ENABLE = 6;
constexpr uint32_t EPD_SPI_HZ = 20'000'000;

// Dedicated SD SPI bus.
constexpr int8_t SD_SCLK = 39;
constexpr int8_t SD_MISO = 40;
constexpr int8_t SD_MOSI = 38;
constexpr int8_t SD_CS = 47;
constexpr uint32_t SD_SPI_HZ = 20'000'000;

// Shared I2C bus.
constexpr int8_t I2C_SDA = 2;
constexpr int8_t I2C_SCL = 1;
constexpr uint32_t I2C_HZ = 400'000;
constexpr uint8_t GSLX680_ADDRESS = 0x40;
constexpr uint8_t PCF8563_ADDRESS = 0x51;
constexpr uint8_t LM3630A_ADDRESS = 0x36;

// Physical inputs.
constexpr int8_t BUTTON_UP = 5;       // active-low
constexpr int8_t BUTTON_DOWN = 7;     // active-low
constexpr int8_t BUTTON_POWER = 8;    // active-high
constexpr uint16_t FRONT_KEY_RAW_X = 0x03a0;
constexpr uint16_t FRONT_KEY_RAW_Y = 0x1020;
constexpr uint32_t FRONT_KEY_LONG_MS = 700;

// Battery and charge status.
constexpr int8_t BATTERY_ADC = 10;
constexpr float BATTERY_DIVIDER = 1.559f;
constexpr int8_t CHARGE_STATUS = 11;

// CrossLink 1.0.10's recovered charge reader selects GPIO11 polarity from the
// LM3630A population probe: frontlight-equipped boards are active-high and
// boards without the optional frontlight are active-low. This retail unit has
// no LM3630A ACK, so its expected charging level is LOW. The signal still needs
// battery/charging/full phase captures before its real-world semantics are
// called verified.
inline bool chargeStatusActiveHigh(const bool frontlightPresent) {
  return frontlightPresent;
}

inline bool chargeStatusAsserted(const int rawLevel,
                                 const bool frontlightPresent) {
  return rawLevel ==
         (chargeStatusActiveHigh(frontlightPresent) ? HIGH : LOW);
}

// Optional frontlight population.
constexpr int8_t FRONTLIGHT_ENABLE = 12;

// Board power latch/rail. Never drive this low during an ordinary diagnostic.
constexpr int8_t POWER_LATCH = 4;

// Official full-range calibration. The final native-Y reflection compensates
// for the logical portrait renderer's clockwise rotation.
inline uint16_t rawTouchToLogicalX(const uint16_t rawY) {
  const uint16_t limitedY = rawY > 680 ? 680 : rawY;
  return static_cast<uint32_t>(limitedY) * 551 / 680;
}

inline uint16_t rawTouchToLogicalY(const uint16_t rawX) {
  const uint16_t limitedX = rawX > 920 ? 920 : rawX;
  return static_cast<uint32_t>(920 - limitedX) * 767 / 920;
}

}  // namespace eego
