#include "UglyAvatarActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Render.h>
#include <esp_system.h>

#include "components/UITheme.h"
#include "util/ScreenshotUtil.h"

void UglyAvatarActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("AVATAR", "onEnter free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  gen_ = std::make_unique<avatar::AvatarGenerator>();
  data_ = std::make_unique<avatar::AvatarData>();
  regenerate();
}

void UglyAvatarActivity::onExit() {
  data_.reset();
  gen_.reset();
  LOG_DBG("AVATAR", "onExit free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  Activity::onExit();
}

void UglyAvatarActivity::regenerate() {
  const uint32_t seed = esp_random() ^ static_cast<uint32_t>(millis());
  const uint32_t t0 = millis();
  gen_->generate(seed, *data_);
  LOG_DBG("AVATAR", "generate seed=%u took=%ums", static_cast<unsigned>(seed), static_cast<unsigned>(millis() - t0));
  requestUpdate();
}

void UglyAvatarActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSave();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    regenerate();
  }
}

void UglyAvatarActivity::onSave() {
  if (!data_) return;

  if (!renderer.storeBwBuffer()) {
    LOG_ERR("AVATAR", "storeBwBuffer failed on save");
    return;
  }

  renderer.clearScreen();
  const avatar::ScreenRect fullViewport{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};
  avatar::drawAvatar(renderer, *data_, fullViewport);

  char filename[64];
  snprintf(filename, sizeof(filename), "/avatars/avatar-%lu.bmp", millis());
  const bool ok = ScreenshotUtil::saveFramebufferAsBmp(filename, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                                       renderer.getDisplayHeight());

  if (ok) {
    LOG_DBG("AVATAR", "saved %s", filename);
    renderer.drawRect(6, 6, renderer.getScreenWidth() - 12, renderer.getScreenHeight() - 12, 2, true);
    renderer.displayBuffer();
    delay(800);
  } else {
    LOG_ERR("AVATAR", "save failed: %s", filename);
  }

  renderer.restoreBwBuffer();
  renderer.displayBuffer(HalDisplay::RefreshMode::HALF_REFRESH);
}

void UglyAvatarActivity::render(RenderLock&&) {
  if (!data_) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UGLY_AVATAR));

  const int viewportTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const avatar::ScreenRect viewport{metrics.contentSidePadding, viewportTop, pageWidth - 2 * metrics.contentSidePadding,
                                    viewportBottom - viewportTop};

  avatar::drawAvatar(renderer, *data_, viewport);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SAVE), "", tr(STR_RANDOM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
