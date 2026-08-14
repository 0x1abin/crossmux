#include "FrontlightAdjustmentActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"
#include "CrossPointSettings.h"

namespace {
constexpr int kBarHeight = 16;
constexpr int kBarTopLeft = -20;
constexpr int kBarBottom = 20;
}  // namespace

void FrontlightAdjustmentActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FrontlightAdjustmentActivity::adjustBrightness(const int delta) {
  const int v = static_cast<int>(SETTINGS.frontlightBrightness) + delta;
  SETTINGS.frontlightBrightness = static_cast<uint8_t>(std::clamp(v, 0, 100));
  Frontlight.setBrightness(SETTINGS.frontlightBrightness);
  requestUpdate();
}

void FrontlightAdjustmentActivity::adjustWarmth(const int delta) {
  const int v = static_cast<int>(SETTINGS.frontlightWarmth) + delta;
  SETTINGS.frontlightWarmth = static_cast<uint8_t>(std::clamp(v, 0, 100));
  Frontlight.setWarmth(SETTINGS.frontlightWarmth);
  requestUpdate();
}

void FrontlightAdjustmentActivity::saveAndFinish() {
  SETTINGS.saveToFile();
  finish();
}

void FrontlightAdjustmentActivity::loop() {
  const int screenWidth = renderer.getScreenWidth();
  const int barWidth = std::min(360, std::max(0, screenWidth - 40));
  const int barX = std::max(0, (screenWidth - barWidth) / 2);
  const int brightnessBarY = 90;
  const int warmthBarY = 185;

  int tx = 0;
  int ty = 0;
  const bool held = mappedInput.isScreenTouchHeld(tx, ty);

  // Live drag on either bar: the value follows the finger until release. Runs
  // before the Back/Confirm handlers so a drag release can't cancel the page.
  auto inBar = [](int ty, int barY) {
    return ty >= barY + kBarTopLeft && ty < barY + kBarHeight + kBarBottom;
  };
  if (held) {
    if (draggingBrightness || (inBar(ty, brightnessBarY) && !inBar(ty, warmthBarY))) {
      draggingBrightness = true;
      const int pct = std::clamp((tx - barX) * 100 / std::max(1, barWidth - 1), 0, 100);
      if (SETTINGS.frontlightBrightness != static_cast<uint8_t>(pct)) {
        SETTINGS.frontlightBrightness = static_cast<uint8_t>(pct);
        Frontlight.setBrightness(SETTINGS.frontlightBrightness);
        requestUpdate();
      }
      return;
    }
    if (draggingWarmth || inBar(ty, warmthBarY)) {
      draggingWarmth = true;
      const int pct = std::clamp((tx - barX) * 100 / std::max(1, barWidth - 1), 0, 100);
      if (SETTINGS.frontlightWarmth != static_cast<uint8_t>(pct)) {
        SETTINGS.frontlightWarmth = static_cast<uint8_t>(pct);
        Frontlight.setWarmth(SETTINGS.frontlightWarmth);
        requestUpdate();
      }
      return;
    }
  } else {
    draggingBrightness = false;
    draggingWarmth = false;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    saveAndFinish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    saveAndFinish();
    return;
  }

  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (inBar(ty, brightnessBarY)) {
      const int pct = std::clamp((tx - barX) * 100 / std::max(1, barWidth - 1), 0, 100);
      SETTINGS.frontlightBrightness = static_cast<uint8_t>(pct);
      Frontlight.setBrightness(SETTINGS.frontlightBrightness);
      requestUpdate();
      return;
    }
    if (inBar(ty, warmthBarY)) {
      const int pct = std::clamp((tx - barX) * 100 / std::max(1, barWidth - 1), 0, 100);
      SETTINGS.frontlightWarmth = static_cast<uint8_t>(pct);
      Frontlight.setWarmth(SETTINGS.frontlightWarmth);
      requestUpdate();
      return;
    }
    if (ty >= renderer.getScreenHeight() - 80) {
      saveAndFinish();
      return;
    }
  }

  // Front buttons adjust brightness, side buttons adjust warmth (small/large steps).
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustBrightness(-5); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustBrightness(5); });
  const bool x3 = gpio.deviceIsX3();
  const int warmthUp = x3 ? -10 : 10;
  const int warmthDown = x3 ? 10 : -10;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, warmthUp] { adjustWarmth(warmthUp); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, warmthDown] { adjustWarmth(warmthDown); });
}

void FrontlightAdjustmentActivity::render(RenderLock&&) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 15, I18N.get(titleId), true, EpdFontFamily::BOLD);

  const int screenWidth = renderer.getScreenWidth();
  const int barWidth = std::min(360, std::max(0, screenWidth - 40));
  const int barX = std::max(0, (screenWidth - barWidth) / 2);

  auto drawSlider = [&](const int barY, const StrId labelId, const uint8_t value) {
    renderer.drawCenteredText(UI_10_FONT_ID, barY - 22, I18N.get(labelId), true);
    renderer.drawRect(barX, barY, barWidth, kBarHeight);
    const int fillWidth = (barWidth - 4) * value / 100;
    if (fillWidth > 0) {
      renderer.fillRect(barX + 2, barY + 2, fillWidth, kBarHeight - 4);
    }
    const int knobX = std::max(barX + 2, barX + 2 + fillWidth - 2);
    renderer.fillRect(knobX, barY - 4, 4, kBarHeight + 8, true);
  };

  drawSlider(90, StrId::STR_FRONTLIGHT_BRIGHTNESS, SETTINGS.frontlightBrightness);
  drawSlider(185, StrId::STR_FRONTLIGHT_WARMTH, SETTINGS.frontlightWarmth);

  // Two-line step hint mirroring the other slider pages.
  renderer.drawCenteredText(SMALL_FONT_ID, 250, tr(STR_STEP_HINT_FRONT), true);
  renderer.drawCenteredText(SMALL_FONT_ID, 272, tr(STR_STEP_HINT_SIDE), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
