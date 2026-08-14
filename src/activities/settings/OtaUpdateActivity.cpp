#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"
#include "util/ButtonNavigator.h"

namespace {
enum ReadyRow {
  CHECK_UPDATES_ROW,
  NIGHTLY_ROW,
  READY_ROW_COUNT,
};

Rect getReadyListRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics, true);
  return Rect{content.x, content.y, content.width, GUI.getListRowStep(false) * READY_ROW_COUNT};
}
}  // namespace

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate(selectedChannel);
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  const char* options[] = {tr(STR_CANCEL), tr(STR_UPDATE)};
  updateConfirmation.show(tr(STR_NEW_UPDATE), options, 2, 0, [this](const int index) {
    if (index == 0) {
      finish();
      return;
    }
    runUpdateInstall();
  });
  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  state = READY;
  selectedChannel = SETTINGS.otaNightlyEnabled ? OtaUpdater::Channel::Nightly : OtaUpdater::Channel::Stable;
  selectedReadyRow = CHECK_UPDATES_ROW;
  waitForConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  requestUpdate();
}

void OtaUpdateActivity::activateReadyRow() {
  switch (static_cast<ReadyRow>(selectedReadyRow)) {
    case CHECK_UPDATES_ROW:
      beginWifiSelection();
      break;
    case NIGHTLY_ROW:
      selectedChannel =
          selectedChannel == OtaUpdater::Channel::Stable ? OtaUpdater::Channel::Nightly : OtaUpdater::Channel::Stable;
      SETTINGS.otaNightlyEnabled = selectedChannel == OtaUpdater::Channel::Nightly;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case READY_ROW_COUNT:
      break;
  }
}

void OtaUpdateActivity::beginWifiSelection() {
  // ActivityManager owns the child across frames, so stack/static lifetime is invalid.
  auto wifiSelection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiSelection) {
    LOG_ERR("OTA", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    state = FAILED;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = WIFI_SELECTION;
    sdFontSystem.releaseLoadedFont(renderer);
  }

  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::move(wifiSelection),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_UPDATE));
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int height = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == READY) {
    GUI.drawSubHeader(
        renderer,
        Rect{safeArea.x, safeArea.y + metrics.topPadding + metrics.headerHeight, safeArea.width, metrics.tabBarHeight},
        tr(STR_CURRENT_VERSION), CROSSPOINT_VERSION);

    const Rect readyList = getReadyListRect(renderer);
    GUI.drawList(
        renderer, readyList, READY_ROW_COUNT, selectedReadyRow,
        [](const int index) {
          return std::string(index == CHECK_UPDATES_ROW ? tr(STR_CHECK_UPDATES) : tr(STR_UPDATE_NIGHTLY));
        },
        nullptr, nullptr,
        [this](const int index) {
          if (index != NIGHTLY_ROW) return std::string();
          return selectedChannel == OtaUpdater::Channel::Nightly ? std::string(tr(STR_STATE_ON))
                                                                 : std::string(tr(STR_STATE_OFF));
        },
        true);
    if (selectedChannel == OtaUpdater::Channel::Nightly) {
      const int warningTop = readyList.y + readyList.height + relatedGap;
      renderer.drawText(SMALL_FONT_ID, safeArea.x + metrics.contentSidePadding, warningTop, tr(STR_NIGHTLY_WARNING));
      renderer.drawText(SMALL_FONT_ID, safeArea.x + metrics.contentSidePadding,
                        warningTop + renderer.getLineHeight(SMALL_FONT_ID) + relatedGap,
                        tr(STR_NIGHTLY_LOCKED_WARNING));
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == CHECKING_FOR_UPDATE) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    const int versionTop = safeArea.y + metrics.topPadding + metrics.headerHeight;
    GUI.drawSubHeader(renderer, Rect{safeArea.x, versionTop, safeArea.width, metrics.tabBarHeight},
                      tr(STR_CURRENT_VERSION), CROSSPOINT_VERSION);
    GUI.drawSubHeader(renderer,
                      Rect{safeArea.x, versionTop + metrics.tabBarHeight, safeArea.width, metrics.tabBarHeight},
                      tr(STR_NEW_VERSION), updater.getLatestVersion().c_str());
    const int summaryLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int summaryTop = safeArea.y + safeArea.height - sectionGap - summaryLineHeight * 2 - relatedGap;
    for (size_t i = 0; i < OtaUpdater::SUMMARY_LINE_COUNT; ++i) {
      const char* line = updater.getSummaryLine(i);
      if (*line != '\0') {
        renderer.drawCenteredText(UI_10_FONT_ID, summaryTop + static_cast<int>(i) * (summaryLineHeight + relatedGap),
                                  line);
      }
    }
    if (updateConfirmation.processRender(renderer, mappedInput)) return;
  } else if (state == UPDATE_IN_PROGRESS) {
    const int blockHeight = titleHeight + sectionGap +
                            GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight) + relatedGap + height;
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
    y += titleHeight + sectionGap;
    y = GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight},
                            static_cast<int>(updaterProgress * 100), 100) +
        relatedGap;
    UITheme::drawCenteredText(
        renderer, textBounds, UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == NO_UPDATE) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    const int failedHeight = titleHeight + (failedDetail != nullptr ? relatedGap + height : 0);
    const int failedTop = SubpageLayout::centeredTop(content, failedHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, failedTop, tr(STR_UPDATE_FAILED), true,
                              EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, failedTop + titleHeight + relatedGap,
                                failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + height);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true,
                              EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap,
                              tr(STR_POWER_ON_HINT));
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      sdFontSystem.ensureLoaded(renderer, false);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (state == READY) {
    if (waitForConfirmRelease) {
      if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
        waitForConfirmRelease = false;
      }
      return;
    }

    const Rect readyList = getReadyListRect(renderer);
    const auto touch = handleListTouch(selectedReadyRow, READY_ROW_COUNT, readyList.y, readyList.height, false);
    if (touch == ListTouchResult::Activated) {
      activateReadyRow();
      return;
    }
    if (touch == ListTouchResult::Consumed) {
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      activateReadyRow();
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
      selectedReadyRow = ButtonNavigator::previousIndex(selectedReadyRow, READY_ROW_COUNT);
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
      selectedReadyRow = ButtonNavigator::nextIndex(selectedReadyRow, READY_ROW_COUNT);
      requestUpdate();
    }
    return;
  }

  if (state == WAITING_CONFIRMATION) {
    if (updateConfirmation.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    finish();
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
