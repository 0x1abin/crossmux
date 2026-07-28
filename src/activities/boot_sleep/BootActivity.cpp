#include "BootActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalOtaSlot.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontCache.h>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "components/FontPreloadView.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  if (mode_ == Mode::PostOta) {
    runPostOta();
    return;
  }

  renderSplash();
}

void BootActivity::renderSplash() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}

void BootActivity::runPostOta() {
  requestUpdateAndWait();
  if (!HalOtaSlot::confirmRunningImage()) {
    failure_.store(Failure::Confirm);
    stage_.store(Stage::Failed);
    requestUpdateAndWait();
    return;
  }
  LOG_INF("OTA", "Running image confirmed after startup display");

  if (!allowAutoPreload_ || SETTINGS.sdFontFlashPreload == 0 || SETTINGS.sdFontFamilyName[0] == '\0') return;

  const auto* family = sdFontSystem.registry().findFamily(SETTINGS.sdFontFamilyName);
  const auto* file = family ? family->findNearestSize(SETTINGS.fontPointSize) : nullptr;
  if (!file || SdCardFontCache::isValidFor(file->path.c_str())) return;

  {
    RenderLock lock(*this);
    sdFontSystem.releaseLoadedFont(renderer);
    completed_.store(0);
    total_.store(1);
    lastRequestedPercent_ = 0;
    preloadPointSize_ = file->pointSize;
    stage_.store(Stage::Copying);
  }
  requestUpdateAndWait();

  const auto result = SdCardFontCache::preload(
      file->path.c_str(),
      [](size_t completed, size_t total, void* context) {
        auto* self = static_cast<BootActivity*>(context);
        self->completed_.store(completed);
        self->total_.store(total);

        const size_t sourceSize = total / 2;
        const Stage nextStage = completed <= sourceSize ? Stage::Copying : Stage::Verifying;
        const bool phaseChanged = self->stage_.exchange(nextStage) != nextStage;
        const unsigned percent = total > 0 ? static_cast<unsigned>(completed * 100 / total) : 0;
        if (phaseChanged || percent == 100 || percent >= self->lastRequestedPercent_ + 10) {
          self->lastRequestedPercent_ = percent;
          self->requestUpdate(true);
        }
      },
      this);

  // The callback only queues refreshes. Keep the verified 100% state visible
  // until the panel has physically completed that frame before showing Ready.
  if (result == SdCardFontCache::Result::Ok) requestUpdateAndWait();

  const bool succeeded = result == SdCardFontCache::Result::Ok || result == SdCardFontCache::Result::AlreadyCached;
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer, succeeded);
    if (succeeded) {
      stage_.store(Stage::Ready);
    } else {
      switch (result) {
        case SdCardFontCache::Result::TooLarge:
          failure_.store(Failure::TooLarge);
          break;
        case SdCardFontCache::Result::Oom:
          failure_.store(Failure::Memory);
          break;
        case SdCardFontCache::Result::OpenFailed:
        case SdCardFontCache::Result::InvalidFont:
        case SdCardFontCache::Result::ReadFailed:
          failure_.store(Failure::SdRead);
          break;
        case SdCardFontCache::Result::EraseFailed:
        case SdCardFontCache::Result::WriteFailed:
          failure_.store(Failure::FlashWrite);
          break;
        case SdCardFontCache::Result::VerifyFailed:
          failure_.store(Failure::Verify);
          break;
        case SdCardFontCache::Result::NotSafe:
          failure_.store(Failure::Confirm);
          break;
        case SdCardFontCache::Result::Ok:
        case SdCardFontCache::Result::AlreadyCached:
          break;
      }
      stage_.store(Stage::Failed);
    }
  }

  LOG_INF("SDFCACHE", "Post-OTA preload for %s: %s", file->path.c_str(), SdCardFontCache::resultName(result));
  requestUpdateAndWait();
}

void BootActivity::render(RenderLock&&) {
  if (mode_ != Mode::PostOta) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const Stage stage = stage_.load();

  const int centerY = pageHeight / 2 - lineHeight;
  switch (stage) {
    case Stage::Confirming:
      renderer.clearScreen();
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOTING));
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_VALIDATING_FIRMWARE));
      break;
    case Stage::Copying:
    case Stage::Verifying:
      fontpreload::draw(renderer, SETTINGS.sdFontFamilyName, preloadPointSize_, completed_.load(), total_.load(),
                        fontpreload::State::Progress);
      break;
    case Stage::Ready:
      fontpreload::draw(renderer, SETTINGS.sdFontFamilyName, preloadPointSize_, completed_.load(), total_.load(),
                        fontpreload::State::Ready);
      break;
    case Stage::Failed: {
      renderer.clearScreen();
      StrId reason = StrId::STR_OTA_CONFIRM_FAILED;
      switch (failure_.load()) {
        case Failure::Confirm:
          reason = StrId::STR_OTA_CONFIRM_FAILED;
          break;
        case Failure::TooLarge:
          reason = StrId::STR_FONT_CACHE_TOO_LARGE;
          break;
        case Failure::Memory:
          reason = StrId::STR_MEMORY_ERROR;
          break;
        case Failure::SdRead:
          reason = StrId::STR_FONT_CACHE_SD_READ_FAILED;
          break;
        case Failure::FlashWrite:
          reason = StrId::STR_FONT_CACHE_FLASH_WRITE_FAILED;
          break;
        case Failure::Verify:
          reason = StrId::STR_FONT_CACHE_VERIFY_FAILED;
          break;
      }
      const StrId header = failure_.load() == Failure::Confirm ? StrId::STR_BOOTING : StrId::STR_FONT_PRELOADING;
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, I18N.get(header));
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, I18N.get(reason), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, tr(STR_FONT_CACHE_USING_SD));
      break;
    }
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
