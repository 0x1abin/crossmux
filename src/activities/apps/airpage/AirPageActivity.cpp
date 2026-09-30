#include "AirPageActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "AirPageDeviceId.h"
#include "AirPageImageRenderer.h"
#include "AirPageWallpaper.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "network/CrossMuxEndpoints.h"
#include "network/HttpDownloader.h"
#include "util/QrUtils.h"
#include "util/TimeUtils.h"

namespace {

namespace fui = freeink::ui;

constexpr uint32_t kWallpaperNoticeDurationMs = 1000u;

uint64_t currentArchiveDateKey() {
  const uint32_t timestamp = TimeUtils::getCurrentValidTimestamp();
  std::tm local{};
  if (!TimeUtils::isClockValid(timestamp) || !TimeUtils::getLocalDateTime(timestamp, local)) return 0;
  return static_cast<uint64_t>(local.tm_year + 1900) * 10000000000u +
         static_cast<uint64_t>(local.tm_mon + 1) * 100000000u + static_cast<uint64_t>(local.tm_mday) * 1000000u +
         static_cast<uint64_t>(local.tm_hour) * 10000u + static_cast<uint64_t>(local.tm_min) * 100u +
         static_cast<uint64_t>(local.tm_sec);
}

bool formatArchiveDate(const uint64_t archiveId, char* buffer, const size_t bufferSize) {
  if (archiveId < 2024010100000000u) return false;
  const unsigned collision = static_cast<unsigned>(archiveId % 100u);
  uint64_t dateKey = archiveId / 100u;
  const unsigned second = static_cast<unsigned>(dateKey % 100u);
  dateKey /= 100u;
  const unsigned minute = static_cast<unsigned>(dateKey % 100u);
  dateKey /= 100u;
  const unsigned hour = static_cast<unsigned>(dateKey % 100u);
  dateKey /= 100u;
  const unsigned day = static_cast<unsigned>(dateKey % 100u);
  dateKey /= 100u;
  const unsigned month = static_cast<unsigned>(dateKey % 100u);
  const unsigned year = static_cast<unsigned>(dateKey / 100u);
  const int written = collision == 0 ? snprintf(buffer, bufferSize, "%04u-%02u-%02u %02u:%02u:%02u", year, month, day,
                                                hour, minute, second)
                                     : snprintf(buffer, bufferSize, "%04u-%02u-%02u %02u:%02u:%02u-%02u", year, month,
                                                day, hour, minute, second, collision);
  return written > 0 && static_cast<size_t>(written) < bufferSize;
}

}  // namespace

void AirPageActivity::onEnter() {
  Activity::onEnter();

  resetUi();
  app.on(ACTION_TOUCH, &AirPageActivity::onTouchAction, this);
  app.on(ACTION_SETTINGS_ROW, &AirPageActivity::onSettingsRow, this);
  app.on(ACTION_HISTORY_ROW, &AirPageActivity::onHistoryRow, this);
  app.setScreen(&AirPageActivity::touchScreen, this);

  phase_ = Phase::Idle;
  screen_ = Screen::Qr;
  notice_ = Notice::None;
  wallpaperResult_ = WallpaperResult::None;
  imageDisplayResult_.store(ImageDisplayResult::None, std::memory_order_relaxed);
  selectedImage_ = {};
  imageNeedsDisplay_ = true;
  waitForInputRelease_ = false;
  displayedScreenWidth_ = 0;
  displayedScreenHeight_ = 0;
  settingsSelection_ = 0;
  historySelection_ = 0;
  settingsNav_.reset();
  historyNav_.reset();
  settingsRows_[0].label = tr(STR_AIRPAGE_MODE_SETTING);
  settingsRows_[0].actionValue = 0;
  settingsRows_[1].label = tr(STR_AIRPAGE_AUTO_WALLPAPER);
  settingsRows_[1].actionValue = 1;
  airpage::AirPageImageRenderer::resetSessionFailures();

  switch (imageStore_.initialize(currentArchiveDateKey())) {
    case airpage::AirPageImageStore::InitializationResult::Empty:
      notice_ = Notice::NoImage;
      break;
    case airpage::AirPageImageStore::InitializationResult::Ready:
      notice_ = Notice::None;
      break;
    case airpage::AirPageImageStore::InitializationResult::Invalid:
      notice_ = Notice::InvalidImage;
      break;
  }
  rebuildHistoryRows();
  autoSleepWallpaper_ = airpage::loadAutoSleepWallpaper();
  airpage::AirPageWallpaper::recoverInterruptedTransaction();

  const std::string& deviceId = airpage::deviceId();
  uploadUrl_.reserve(64 + deviceId.size());
  uploadUrl_ = "https://";
  uploadUrl_ += CrossMuxEndpoints::AIRPAGE_SUBDOMAIN;
  uploadUrl_ += CrossMuxEndpoints::host();
  uploadUrl_ += "/?id=";
  uploadUrl_ += deviceId;
  char displayParams[48];
  snprintf(displayParams, sizeof(displayParams), "&w=%u&h=%u&mode=gray4",
           static_cast<unsigned>(renderer.getDisplayHeight()), static_cast<unsigned>(renderer.getDisplayWidth()));
  uploadUrl_ += displayParams;

  downloadUrl_.reserve(64 + deviceId.size());
  downloadUrl_ = "https://";
  downloadUrl_ += CrossMuxEndpoints::AIRPAGE_SUBDOMAIN;
  downloadUrl_ += CrossMuxEndpoints::host();
  downloadUrl_ += "/api/device/";
  downloadUrl_ += deviceId;
  downloadUrl_ += "/latest";

  legacyDownloadUrl_.reserve(64 + deviceId.size());
  legacyDownloadUrl_ = "https://";
  legacyDownloadUrl_ += CrossMuxEndpoints::AIRPAGE_SUBDOMAIN;
  legacyDownloadUrl_ += CrossMuxEndpoints::host();
  legacyDownloadUrl_ += "/api/device/";
  legacyDownloadUrl_ += deviceId;
  legacyDownloadUrl_ += "/latest.bmp";

  const auto connectionEvent = connection_.begin(airpage::loadRealtimeMode());
  applyConnectionEvent(connectionEvent);
  LOG_DBG("AIRP", "onEnter activity=%u free=%u largest=%u id=%s cached=%d realtime=%d",
          static_cast<unsigned>(sizeof(*this)), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), deviceId.c_str(), imageStore_.hasImage() ? 1 : 0,
          connection_.realtime() ? 1 : 0);
  requestUpdate();
  if (connectionEvent == airpage::AirPageConnection::Event::WifiRequired) openWifiSelection(false);
}

void AirPageActivity::onExit() {
  connection_.stop();
  airpage::AirPageImageRenderer::releaseSessionResources();
  LOG_DBG("AIRP", "onExit free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  Activity::onExit();
#if FREEINK_DEVICE_EEGO_A4
  // EEGO can render the AirPage image as a grayscale full-screen frame; force
  // a clean first frame after exit so its pixels do not ghost into the next
  // activity (same A4-only treatment as the EPUB reader).
  renderer.requestNextFullRefresh();
#endif
}

bool AirPageActivity::preventAutoSleep() { return phase_ != Phase::Idle || connection_.preventsAutoSleep(); }

bool AirPageActivity::processImageDisplayResult() {
  const ImageDisplayResult result = imageDisplayResult_.exchange(ImageDisplayResult::None, std::memory_order_acquire);
  switch (result) {
    case ImageDisplayResult::None:
      return false;

    case ImageDisplayResult::Success: {
      const bool newlyDownloaded = selectedImage_.current && imageStore_.hasPendingDownload();
      if (newlyDownloaded) {
        imageStore_.commitDisplayedDownload(currentArchiveDateKey());
        imageStore_.selectCurrent(selectedImage_);
        rebuildHistoryRows();
        if (autoSleepWallpaper_ && !airpage::AirPageWallpaper::install(selectedImage_)) {
          notice_ = Notice::WallpaperFailed;
          wallpaperResult_ = WallpaperResult::Failed;
          requestUpdate();
        }
      }
      return true;
    }

    case ImageDisplayResult::Failure:
      airpage::AirPageImageRenderer::resetSessionFailures();
      switch (imageStore_.rejectDisplayedImage(selectedImage_)) {
        case airpage::AirPageImageStore::RejectResult::CurrentRestored:
        case airpage::AirPageImageStore::RejectResult::CurrentInvalid:
          setAirPageScreen(Screen::Qr);
          break;
        case airpage::AirPageImageStore::RejectResult::HistoryInvalid:
          if (historySelection_ >= static_cast<int>(imageStore_.historyCount())) {
            historySelection_ = imageStore_.historyCount() == 0 ? 0 : static_cast<int>(imageStore_.historyCount() - 1);
          }
          setAirPageScreen(Screen::History);
          break;
      }
      rebuildHistoryRows();
      notice_ = Notice::InvalidImage;
      imageNeedsDisplay_ = true;
      requestUpdate();
      return true;
  }
  return false;
}

void AirPageActivity::applyConnectionEvent(const airpage::AirPageConnection::Event event) {
  switch (event) {
    case airpage::AirPageConnection::Event::None:
      return;
    case airpage::AirPageConnection::Event::StateChanged:
      notice_ = Notice::None;
      break;
    case airpage::AirPageConnection::Event::WifiRequired:
      notice_ = Notice::WifiRequired;
      setAirPageScreen(Screen::Qr);
      break;
    case airpage::AirPageConnection::Event::WifiFailed:
      notice_ = Notice::WifiFailed;
      setAirPageScreen(Screen::Qr);
      break;
    case airpage::AirPageConnection::Event::RealtimeRetrying:
      notice_ = Notice::RealtimeRetrying;
      setAirPageScreen(Screen::Qr);
      break;
    case airpage::AirPageConnection::Event::RealtimePaused:
      notice_ = Notice::RealtimePaused;
      setAirPageScreen(Screen::Qr);
      break;
    case airpage::AirPageConnection::Event::PushRequested:
      queueFetch();
      return;
  }
  requestUpdate();
}

void AirPageActivity::clearConnectionNotice() {
  switch (notice_) {
    case Notice::WifiRequired:
    case Notice::WifiFailed:
    case Notice::RealtimeRetrying:
    case Notice::RealtimePaused:
      notice_ = imageStore_.hasImage() ? Notice::None : Notice::NoImage;
      break;
    case Notice::None:
    case Notice::NoImage:
    case Notice::InvalidImage:
    case Notice::DownloadFailed:
    case Notice::SettingsSaveFailed:
    case Notice::WallpaperFailed:
      break;
  }
}

void AirPageActivity::loop() {
  if (processImageDisplayResult()) return;
  if (consumeInputReleaseBarrier()) return;

  if (imageMenu_.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (!imageMenu_.isActive()) {
      closeRouting();
      imageNeedsDisplay_ = true;
      requestUpdate();
    }
    return;
  }

  if (phase_ == Phase::Idle) {
    const auto touch = routeTouch(mappedInput);
    if (touch.routed && app.invalidated()) requestUpdate();
    if (touch) return;

    if (screen_ == Screen::Settings || screen_ == Screen::History) {
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
        const int count = screen_ == Screen::Settings ? kSettingsRows : static_cast<int>(imageStore_.historyCount());
        bool moved = false;
        {
          RenderLock lock(*this);
          fui::ListNav& nav = screen_ == Screen::Settings ? settingsNav_ : historyNav_;
          const int delta = swipe == MappedInputManager::SwipeDir::Up ? nav.pageRows() : -nav.pageRows();
          moved = nav.scrollBy(delta, count);
        }
        if (moved) requestUpdate();
        return;
      }
    }
  }

  switch (screen_) {
    case Screen::Qr:
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        activityManager.goToApps();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        openSettings();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
        openHistory();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
        handleRefresh();
        return;
      }
      break;

    case Screen::Settings: {
      if (mappedInput.wasAnyReleased() && notice_ == Notice::SettingsSaveFailed) notice_ = Notice::None;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        setAirPageScreen(Screen::Qr);
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        applySettingsSelection();
        return;
      }

      bool navigated = false;
      const auto moveSelection = [this, &navigated](const int index) {
        navigated = true;
        moveSettingsSelection(index);
      };
      buttonNavigator_.onNextRelease(
          [this, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(settingsSelection_, kSettingsRows)); });
      buttonNavigator_.onPreviousRelease(
          [this, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(settingsSelection_, kSettingsRows)); });
      buttonNavigator_.onNextContinuous([this, &moveSelection] {
        moveSelection(ButtonNavigator::nextPageIndex(settingsSelection_, kSettingsRows, settingsNav_.pageRows()));
      });
      buttonNavigator_.onPreviousContinuous([this, &moveSelection] {
        moveSelection(ButtonNavigator::previousPageIndex(settingsSelection_, kSettingsRows, settingsNav_.pageRows()));
      });
      if (navigated) return;

      break;
    }

    case Screen::History: {
      if (mappedInput.wasAnyReleased() && notice_ == Notice::InvalidImage) notice_ = Notice::None;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        setAirPageScreen(Screen::Qr);
        requestUpdate();
        return;
      }
      const size_t historyCount = imageStore_.historyCount();
      if (historyCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        openSelectedHistoryImage();
        return;
      }
      if (historyCount == 0) break;

      bool navigated = false;
      const auto moveSelection = [this, &navigated](const int index) {
        navigated = true;
        moveHistorySelection(index);
      };
      buttonNavigator_.onNextRelease([this, historyCount, &moveSelection] {
        moveSelection(ButtonNavigator::nextIndex(historySelection_, static_cast<int>(historyCount)));
      });
      buttonNavigator_.onPreviousRelease([this, historyCount, &moveSelection] {
        moveSelection(ButtonNavigator::previousIndex(historySelection_, static_cast<int>(historyCount)));
      });
      buttonNavigator_.onNextContinuous([this, historyCount, &moveSelection] {
        moveSelection(
            ButtonNavigator::nextPageIndex(historySelection_, static_cast<int>(historyCount), historyNav_.pageRows()));
      });
      buttonNavigator_.onPreviousContinuous([this, historyCount, &moveSelection] {
        moveSelection(ButtonNavigator::previousPageIndex(historySelection_, static_cast<int>(historyCount),
                                                         historyNav_.pageRows()));
      });
      if (navigated) return;
      break;
    }

    case Screen::Image:
      if (mappedInput.wasAnyReleased()) wallpaperResult_ = WallpaperResult::None;
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        setAirPageScreen(Screen::Qr);
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        openWallpaperConfirmation();
        return;
      }
      break;
  }

  if (phase_ == Phase::FetchRequested) {
    phase_ = Phase::Fetching;
    doFetch();
    phase_ = Phase::Idle;
    requestUpdate();
    return;
  }
  applyConnectionEvent(connection_.pump(phase_ == Phase::Idle && screen_ != Screen::Settings));
}

void AirPageActivity::setAirPageScreen(const Screen screen) {
  if (screen_ == screen) return;
  closeRouting();
#if FREEINK_DEVICE_EEGO_A4
  // Leaving the full-screen image for a normal UI page (QR/settings/history):
  // the image may have been driven as a grayscale frame whose residue a plain
  // FAST diff won't scrub. Force a clean first frame on the A4.
  if (screen_ == Screen::Image && screen != Screen::Image) renderer.requestNextFullRefresh();
#endif
  screen_ = screen;
}

void AirPageActivity::touchScreen(UiScreen& screen, void* user) {
  static_cast<AirPageActivity*>(user)->buildTouchScreen(screen);
}

void AirPageActivity::buildTouchScreen(UiScreen& screen) {
  if (phase_ != Phase::Idle || wallpaperResult_ != WallpaperResult::None) return;

  switch (screen_) {
    case Screen::Qr: {
      if (!mappedInput.hasTouch()) return;
      const fui::FooterAction actions[] = {
          {tr(STR_BACK), ACTION_TOUCH, static_cast<int16_t>(TouchAction::BackToApps)},
          {tr(STR_AIRPAGE_SETTINGS_ACTION), ACTION_TOUCH, static_cast<int16_t>(TouchAction::Settings)},
          {tr(STR_AIRPAGE_IMAGES_ACTION), ACTION_TOUCH, static_cast<int16_t>(TouchAction::Images)},
          {refreshActionText(), ACTION_TOUCH, static_cast<int16_t>(TouchAction::Refresh)},
      };
      screen.footer(actions, 4);
      return;
    }
    case Screen::Image: {
      if (!mappedInput.hasTouch()) return;
      const fui::TapZone zone{screen.frame().screen(), ACTION_TOUCH, static_cast<int16_t>(TouchAction::OpenImageMenu)};
      fui::TapZonesProps props;
      props.zones = &zone;
      props.count = 1;
      fui::tapZones(screen.frame(), screen.frame().screen(), props);
      return;
    }
    case Screen::Settings: {
      const Rect content = contentViewport();
      screen.setContentMargin(fui::Insets{static_cast<int16_t>(content.y),
                                          static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
                                          static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height),
                                          static_cast<int16_t>(content.x)});
      settingsRows_[0].value = connection_.realtime() ? tr(STR_AIRPAGE_MODE_REALTIME) : tr(STR_AIRPAGE_MODE_MANUAL);
      settingsRows_[1].value = autoSleepWallpaper_ ? tr(STR_AIRPAGE_SETTING_ON) : tr(STR_AIRPAGE_SETTING_OFF);
      fui::ListProps props;
      props.items = settingsRows_;
      props.count = kSettingsRows;
      props.action = ACTION_SETTINGS_ROW;
      props.inputMask = fui::InputTouch;
      props.valueInset = 8;
      int16_t rowHeight = screen.theme().rowHeight;
      if (!mappedInput.hasTouch()) {
        rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight);
        props.rowHeight = rowHeight;
      }
      settingsNav_.selected = settingsSelection_;
      settingsNav_.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, kSettingsRows, props);
      screen.list(props);
      return;
    }
    case Screen::History: {
      const Rect content = contentViewport();
      screen.setContentMargin(fui::Insets{static_cast<int16_t>(content.y),
                                          static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
                                          static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height),
                                          static_cast<int16_t>(content.x)});
      const int count = static_cast<int>(imageStore_.historyCount());
      if (count == 0) {
        screen.centeredText(tr(STR_AIRPAGE_NO_IMAGE), screen.theme().bodyText);
        return;
      }
      fui::ListProps props;
      props.items = historyRows_;
      props.count = static_cast<uint16_t>(count);
      props.action = ACTION_HISTORY_ROW;
      props.inputMask = fui::InputTouch;
      int16_t rowHeight = screen.theme().rowHeight;
      if (!mappedInput.hasTouch()) {
        rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listWithSubtitleRowHeight);
        props.rowHeight = rowHeight;
      }
      historyNav_.selected = historySelection_;
      historyNav_.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, count, props);
      screen.list(props);
      return;
    }
  }
}

void AirPageActivity::onTouchAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<AirPageActivity*>(user);
  self->app.clearTapFlash();
  self->applyTouchAction(static_cast<TouchAction>(event.value));
}

void AirPageActivity::onSettingsRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<AirPageActivity*>(user);
  if (self->screen_ != Screen::Settings || event.value < 0 || event.value >= kSettingsRows) return;
  self->app.clearTapFlash();
  self->moveSettingsSelection(event.value);
  self->applySettingsSelection();
}

void AirPageActivity::onHistoryRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<AirPageActivity*>(user);
  if (self->screen_ != Screen::History || event.value < 0 ||
      event.value >= static_cast<int>(self->imageStore_.historyCount())) {
    return;
  }
  self->app.clearTapFlash();
  self->moveHistorySelection(event.value);
  self->openSelectedHistoryImage();
}

void AirPageActivity::applyTouchAction(const TouchAction action) {
  switch (action) {
    case TouchAction::BackToApps:
      activityManager.goToApps();
      return;
    case TouchAction::ShowQr:
      setAirPageScreen(Screen::Qr);
      requestUpdate();
      return;
    case TouchAction::Settings:
      openSettings();
      return;
    case TouchAction::Images:
      openHistory();
      return;
    case TouchAction::Refresh:
      handleRefresh();
      return;
    case TouchAction::SetWallpaper:
      openWallpaperConfirmation();
      return;
    case TouchAction::OpenImageMenu:
      openImageMenu();
      return;
  }
}

void AirPageActivity::openImageMenu() {
  if (phase_ != Phase::Idle || screen_ != Screen::Image || selectedImage_.path[0] == '\0') return;
  static constexpr int OPTION_COUNT = 5;
  static constexpr TouchAction ACTIONS[OPTION_COUNT] = {TouchAction::ShowQr, TouchAction::Settings, TouchAction::Images,
                                                        TouchAction::Refresh, TouchAction::SetWallpaper};
  const char* options[OPTION_COUNT] = {tr(STR_DISPLAY_QR), tr(STR_AIRPAGE_SETTINGS_ACTION),
                                       tr(STR_AIRPAGE_IMAGES_ACTION), refreshActionText(), tr(STR_SET_SLEEP_COVER)};
  imageMenu_.show(tr(STR_AIRPAGE_TITLE), options, OPTION_COUNT, 0, [this](const int index) {
    if (index < 0 || index >= OPTION_COUNT) return;
    applyTouchAction(ACTIONS[index]);
  });
  closeRouting();
  requestUpdate();
}

void AirPageActivity::openSettings() {
  if (phase_ != Phase::Idle) return;
  setAirPageScreen(Screen::Settings);
  settingsNav_.reset();
  moveSettingsSelection(0);
  requestUpdate();
}

void AirPageActivity::moveSettingsSelection(const int index) {
  {
    RenderLock lock(*this);
    settingsSelection_ = index;
    settingsNav_.selected = index;
    settingsNav_.follow(kSettingsRows);
  }
  requestUpdate();
}

void AirPageActivity::applySettingsSelection() {
  switch (static_cast<SettingRow>(settingsSelection_)) {
    case SettingRow::Mode: {
      const bool enabled = !connection_.realtime();
      if (!airpage::saveRealtimeMode(enabled)) {
        notice_ = Notice::SettingsSaveFailed;
        requestUpdate();
        return;
      }
      const auto event = connection_.setRealtime(enabled);
      if (enabled) {
        applyConnectionEvent(event);
        if (!connection_.wifiConnected()) openWifiSelection(false);
      } else {
        clearConnectionNotice();
        requestUpdate();
      }
      break;
    }
    case SettingRow::AutoWallpaper: {
      const bool enabled = !autoSleepWallpaper_;
      if (!airpage::saveAutoSleepWallpaper(enabled)) {
        notice_ = Notice::SettingsSaveFailed;
        requestUpdate();
        return;
      }
      autoSleepWallpaper_ = enabled;
      requestUpdate();
      break;
    }
    case SettingRow::Count:
      return;
  }
}

void AirPageActivity::openHistory() {
  if (phase_ != Phase::Idle) return;
  historyNav_.reset();
  historySelection_ = 0;
  rebuildHistoryRows();
  setAirPageScreen(Screen::History);
  requestUpdate();
}

void AirPageActivity::moveHistorySelection(const int index) {
  {
    RenderLock lock(*this);
    historySelection_ = index;
    historyNav_.selected = index;
    historyNav_.follow(static_cast<int>(imageStore_.historyCount()));
  }
  requestUpdate();
}

void AirPageActivity::rebuildHistoryRows() {
  RenderLock lock(*this);
  const int count = static_cast<int>(imageStore_.historyCount());
  historySelection_ = std::min(historySelection_, std::max(0, count - 1));
  for (int i = 0; i < count; ++i) {
    const auto& entry = imageStore_.historyEntry(static_cast<size_t>(i));
    if (entry.isCurrent()) {
      snprintf(historyLabels_[i], sizeof(historyLabels_[i]), "%s", tr(STR_AIRPAGE_CURRENT_IMAGE));
    } else if (!formatArchiveDate(entry.archiveId, historyLabels_[i], sizeof(historyLabels_[i]))) {
      snprintf(historyLabels_[i], sizeof(historyLabels_[i]), "%s %d", tr(STR_AIRPAGE_IMAGE_LABEL), i + 1);
    }
    const char* format = entry.image.format == airpage::ImageFormat::Jpeg ? "JPEG" : "BMP";
    snprintf(historySubtitles_[i], sizeof(historySubtitles_[i]), "%s · %d×%d", format, entry.image.width,
             entry.image.height);
    historyRows_[i].label = historyLabels_[i];
    historyRows_[i].subtitle = historySubtitles_[i];
    historyRows_[i].actionValue = static_cast<int16_t>(i);
  }
  historyNav_.selected = historySelection_;
  historyNav_.scrollBy(0, count);
  historyNav_.follow(count);
}

void AirPageActivity::openSelectedHistoryImage() {
  if (historySelection_ < 0 || !imageStore_.selectHistory(static_cast<size_t>(historySelection_), selectedImage_)) {
    if (historySelection_ >= static_cast<int>(imageStore_.historyCount())) {
      historySelection_ = imageStore_.historyCount() == 0 ? 0 : static_cast<int>(imageStore_.historyCount() - 1);
    }
    rebuildHistoryRows();
    notice_ = Notice::InvalidImage;
    requestUpdate();
    return;
  }
  wallpaperResult_ = WallpaperResult::None;
  setAirPageScreen(Screen::Image);
  imageNeedsDisplay_ = true;
  requestUpdate();
}

void AirPageActivity::openWallpaperConfirmation() {
  if (phase_ != Phase::Idle || selectedImage_.path[0] == '\0') return;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, tr(STR_AIRPAGE_SET_WALLPAPER_TITLE), tr(STR_AIRPAGE_SET_WALLPAPER_CONFIRM));
  if (!confirmation) {
    LOG_ERR("AIRP", "OOM: ConfirmationActivity (%u bytes)", static_cast<unsigned>(sizeof(ConfirmationActivity)));
    wallpaperResult_ = WallpaperResult::Failed;
    imageNeedsDisplay_ = true;
    requestUpdate();
    return;
  }

  imageNeedsDisplay_ = true;
  startActivityForResult(std::move(confirmation),
                         [this](const ActivityResult& result) { handleWallpaperResult(result); });
}

void AirPageActivity::handleWallpaperResult(const ActivityResult& result) {
  waitForInputRelease_ = true;
  imageNeedsDisplay_ = true;
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  phase_ = Phase::WallpaperWriting;
  wallpaperResult_ =
      airpage::AirPageWallpaper::install(selectedImage_) ? WallpaperResult::Saved : WallpaperResult::Failed;
  phase_ = Phase::WallpaperNotice;
  requestUpdateAndWait();
  delay(kWallpaperNoticeDurationMs);

  wallpaperResult_ = WallpaperResult::None;
  phase_ = Phase::Idle;
  requestUpdateAndWait();
}

void AirPageActivity::handleRefresh() {
  if (phase_ != Phase::Idle) return;
  if (!connection_.wifiConnected()) {
    openWifiSelection(true);
    return;
  }

  connection_.prepareRefresh();
  queueFetch();
}

void AirPageActivity::queueFetch() {
  if (phase_ != Phase::Idle || screen_ == Screen::Settings) return;
  closeRouting();
  phase_ = Phase::FetchRequested;
  requestUpdate();
}

void AirPageActivity::openWifiSelection(const bool fetchAfterConnect) {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true);
  if (!wifi) {
    LOG_ERR("AIRP", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    notice_ = Notice::WifiFailed;
    setAirPageScreen(Screen::Qr);
    requestUpdate();
    return;
  }

  imageNeedsDisplay_ = true;
  startActivityForResult(std::move(wifi), [this, fetchAfterConnect](const ActivityResult& result) {
    handleWifiResult(result, fetchAfterConnect);
  });
}

void AirPageActivity::handleWifiResult(const ActivityResult& result, const bool fetchAfterConnect) {
  waitForInputRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                         mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                         mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                         mappedInput.isPressed(MappedInputManager::Button::NavNext);
  imageNeedsDisplay_ = true;

  const bool connected = !result.isCancelled && connection_.wifiConnected();
  const auto event = connection_.acceptWifiSelection(connected);
  if (!connected) {
    if (connection_.realtime() || !imageStore_.hasImage()) applyConnectionEvent(event);
    requestUpdate();
    return;
  }

  notice_ = Notice::None;
  applyConnectionEvent(event);
  if (fetchAfterConnect) queueFetch();
}

bool AirPageActivity::consumeInputReleaseBarrier() {
  if (!waitForInputRelease_) return false;
  const bool anyPressed = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                          mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavNext);
  if (!anyPressed) waitForInputRelease_ = false;
  return true;
}

void AirPageActivity::doFetch() {
  if (!connection_.wifiConnected()) {
    applyConnectionEvent(connection_.handleWifiFailure());
    return;
  }

  if (!imageStore_.ensureDirectories()) {
    notice_ = Notice::DownloadFailed;
    setAirPageScreen(Screen::Qr);
    return;
  }

  HttpDownloader::DownloadError error =
      HttpDownloader::downloadToFile(downloadUrl_, airpage::AirPageImageStore::kDownloadPartPath);
  if (error != HttpDownloader::OK) {
    error = HttpDownloader::downloadToFile(legacyDownloadUrl_, airpage::AirPageImageStore::kDownloadPartPath);
  }
  if (error != HttpDownloader::OK) {
    LOG_ERR("AIRP", "Download failed: %d", static_cast<int>(error));
    notice_ = Notice::DownloadFailed;
    setAirPageScreen(Screen::Qr);
    return;
  }

  switch (imageStore_.stageDownloadedImage(currentArchiveDateKey())) {
    case airpage::AirPageImageStore::StageResult::Failed:
      notice_ = Notice::DownloadFailed;
      setAirPageScreen(Screen::Qr);
      return;

    case airpage::AirPageImageStore::StageResult::Unchanged:
      airpage::AirPageImageRenderer::resetSessionFailures();
      imageStore_.selectCurrent(selectedImage_);
      setAirPageScreen(Screen::Image);
      imageNeedsDisplay_ = true;
      notice_ = Notice::None;
      LOG_INF("AIRP", "Fetched image is unchanged");
      return;

    case airpage::AirPageImageStore::StageResult::PendingDisplay:
      airpage::AirPageImageRenderer::resetSessionFailures();
      imageStore_.selectCurrent(selectedImage_);
      imageNeedsDisplay_ = true;
      notice_ = Notice::None;
      setAirPageScreen(Screen::Image);
      LOG_INF("AIRP", "Fetched latest image; awaiting display validation");
      return;
  }
}

Rect AirPageActivity::contentViewport() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  const int contentTop = safeArea.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentHeight = safeArea.height - metrics.topPadding - metrics.headerHeight - metrics.verticalSpacing * 2;
  if (mappedInput.hasTouch() && screen_ == Screen::Qr) {
    contentHeight = std::max(0, contentHeight - app.theme().footerHeight - metrics.verticalSpacing);
  }
  return Rect{safeArea.x, contentTop, safeArea.width, contentHeight};
}

void AirPageActivity::render(RenderLock&&) {
  const Rect fullScreen{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};

  if (phase_ == Phase::WallpaperNotice) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    const char* message =
        wallpaperResult_ == WallpaperResult::Saved ? tr(STR_AIRPAGE_WALLPAPER_SAVED) : tr(STR_AIRPAGE_WALLPAPER_FAILED);
    GUI.drawPopup(renderer, message);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (screen_ == Screen::Image && (phase_ == Phase::FetchRequested || phase_ == Phase::Fetching)) {
    renderer.setRenderMode(GfxRenderer::BW);
    GUI.drawPopup(renderer, tr(STR_UPDATING));
    return;
  }

  if (imageMenu_.processRender(renderer, mappedInput)) return;

  if (screen_ == Screen::Image && phase_ == Phase::Idle) {
    const bool screenSizeChanged =
        displayedScreenWidth_ != fullScreen.width || displayedScreenHeight_ != fullScreen.height;
    if (imageNeedsDisplay_ || screenSizeChanged) {
      const bool rendered = airpage::AirPageImageRenderer::render(renderer, fullScreen, selectedImage_);
      if (rendered) {
        imageNeedsDisplay_ = false;
        displayedScreenWidth_ = fullScreen.width;
        displayedScreenHeight_ = fullScreen.height;
        if (wallpaperResult_ != WallpaperResult::None) {
          const char* message = wallpaperResult_ == WallpaperResult::Saved ? tr(STR_AIRPAGE_WALLPAPER_SAVED)
                                                                           : tr(STR_AIRPAGE_WALLPAPER_FAILED);
          GUI.drawPopup(renderer, message);
          renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        }
        imageDisplayResult_.store(ImageDisplayResult::Success, std::memory_order_release);
        if (mappedInput.hasTouch()) renderUi();
      } else {
        imageDisplayResult_.store(ImageDisplayResult::Failure, std::memory_order_release);
      }
      return;
    }

    if (wallpaperResult_ != WallpaperResult::None) {
      const char* message = wallpaperResult_ == WallpaperResult::Saved ? tr(STR_AIRPAGE_WALLPAPER_SAVED)
                                                                       : tr(STR_AIRPAGE_WALLPAPER_FAILED);
      GUI.drawPopup(renderer, message);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
    if (mappedInput.hasTouch()) renderUi();
    return;
  }

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 screenTitle());

  const Rect content = contentViewport();
  if (phase_ != Phase::Idle) {
    renderStatus(content, tr(STR_AIRPAGE_LOADING));
  } else {
    switch (screen_) {
      case Screen::Qr:
        renderQr(content);
        break;
      case Screen::Settings:
        break;
      case Screen::History:
        break;
      case Screen::Image:
        renderStatus(content, tr(STR_AIRPAGE_INVALID_IMAGE));
        break;
    }
  }

  switch (screen_) {
    case Screen::Qr: {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_AIRPAGE_SETTINGS_ACTION),
                                                tr(STR_AIRPAGE_IMAGES_ACTION), refreshActionText());
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (notice_ == Notice::InvalidImage) GUI.drawPopup(renderer, noticeText());
      break;
    }
    case Screen::Settings: {
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (notice_ == Notice::SettingsSaveFailed) GUI.drawPopup(renderer, noticeText());
      break;
    }
    case Screen::History: {
      const bool hasHistory = imageStore_.historyCount() != 0;
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), hasHistory ? tr(STR_OPEN) : "",
                                                hasHistory ? tr(STR_DIR_UP) : "", hasHistory ? tr(STR_DIR_DOWN) : "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case Screen::Image:
      break;
  }

  if (mappedInput.hasTouch() || screen_ == Screen::Settings || screen_ == Screen::History) renderUi();

  renderer.displayBuffer();
  imageNeedsDisplay_ = true;
  displayedScreenWidth_ = 0;
  displayedScreenHeight_ = 0;
}

const char* AirPageActivity::noticeText() const {
  switch (notice_) {
    case Notice::None:
      return tr(STR_AIRPAGE_QR_HINT);
    case Notice::NoImage:
      return tr(STR_AIRPAGE_NO_IMAGE);
    case Notice::InvalidImage:
      return tr(STR_AIRPAGE_INVALID_IMAGE);
    case Notice::WifiRequired:
      return tr(STR_AIRPAGE_WIFI_REQUIRED);
    case Notice::WifiFailed:
      return tr(STR_AIRPAGE_WIFI_FAILED);
    case Notice::DownloadFailed:
      return imageStore_.hasImage() ? tr(STR_AIRPAGE_FETCH_FAILED_KEEPING_IMAGE) : tr(STR_AIRPAGE_FETCH_FAILED);
    case Notice::RealtimeRetrying:
      return tr(STR_AIRPAGE_REALTIME_RETRYING);
    case Notice::RealtimePaused:
      return tr(STR_AIRPAGE_REALTIME_PAUSED);
    case Notice::SettingsSaveFailed:
      return tr(STR_AIRPAGE_SETTINGS_SAVE_FAILED);
    case Notice::WallpaperFailed:
      return tr(STR_AIRPAGE_WALLPAPER_FAILED);
  }
  return "";
}

const char* AirPageActivity::connectionText() const {
  switch (connection_.state()) {
    case airpage::AirPageConnection::State::Off:
    case airpage::AirPageConnection::State::Backoff:
    case airpage::AirPageConnection::State::Paused:
      return "";
    case airpage::AirPageConnection::State::WifiConnecting:
      return tr(STR_AIRPAGE_WIFI_CONNECTING);
    case airpage::AirPageConnection::State::WifiOnline:
      return tr(STR_CONNECTED);
    case airpage::AirPageConnection::State::BrokerConnecting:
      return tr(STR_AIRPAGE_REALTIME_CONNECTING);
    case airpage::AirPageConnection::State::Online:
      return tr(STR_AIRPAGE_REALTIME_LIVE);
  }
  return "";
}

const char* AirPageActivity::refreshActionText() const {
  switch (connection_.state()) {
    case airpage::AirPageConnection::State::Paused:
      return connection_.wifiConnected() ? tr(STR_RETRY) : tr(STR_CONNECT);
    case airpage::AirPageConnection::State::Backoff:
      return tr(STR_RETRY);
    case airpage::AirPageConnection::State::Off:
      return connection_.wifiConnected() ? tr(STR_AIRPAGE_REFRESH) : tr(STR_CONNECT);
    case airpage::AirPageConnection::State::WifiConnecting:
      return tr(STR_CONNECT);
    case airpage::AirPageConnection::State::WifiOnline:
    case airpage::AirPageConnection::State::BrokerConnecting:
    case airpage::AirPageConnection::State::Online:
      return tr(STR_AIRPAGE_REFRESH);
  }
  return tr(STR_AIRPAGE_REFRESH);
}

void AirPageActivity::renderQr(const Rect& viewport) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int reservedTextHeight = lineHeight * 2 + metrics.verticalSpacing * 2;
  const int qrSize =
      std::max(1, std::min(viewport.width - metrics.contentSidePadding * 2, viewport.height - reservedTextHeight));
  const Rect qrBounds{viewport.x + (viewport.width - qrSize) / 2, viewport.y, qrSize, qrSize};
  QrUtils::drawQrCode(renderer, qrBounds, uploadUrl_);

  const int hintY = qrBounds.y + qrBounds.height + metrics.verticalSpacing;
  GUI.drawHelpText(renderer, Rect{viewport.x, hintY, viewport.width, lineHeight}, noticeText());

  const char* state = connectionText();
  if (state[0] != '\0') {
    GUI.drawHelpText(renderer,
                     Rect{viewport.x, hintY + lineHeight + metrics.verticalSpacing, viewport.width, lineHeight}, state);
  }
}

void AirPageActivity::renderStatus(const Rect& viewport, const char* msg) {
  UITheme::drawCenteredWrappedText(renderer, viewport, UI_12_FONT_ID, msg, 2);
}

const char* AirPageActivity::screenTitle() const {
  switch (screen_) {
    case Screen::Qr:
    case Screen::Image:
      return tr(STR_AIRPAGE_TITLE);
    case Screen::Settings:
      return tr(STR_AIRPAGE_SETTINGS_TITLE);
    case Screen::History:
      return tr(STR_AIRPAGE_HISTORY_TITLE);
  }
  return tr(STR_AIRPAGE_TITLE);
}
