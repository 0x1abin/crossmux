#include "OtaUpdateActivity.h"

#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"
#include "util/ButtonNavigator.h"

namespace {
namespace fui = freeink::ui;

enum ReadyRow {
  CHECK_UPDATES_ROW,
  NIGHTLY_ROW,
  READY_ROW_COUNT,
};

constexpr uint8_t RELEASE_NOTE_MAX_LINES = 4;

fui::TextStyle releaseNoteStyle() {
  fui::TextStyle style;
  style.font = fui::GfxRendererTarget::FONT_BODY;
  style.maxLines = RELEASE_NOTE_MAX_LINES;
  return style;
}

Rect releaseNotesBody(const Rect& safeArea, const ThemeMetrics& metrics, const bool firstPage, const int bottomInset) {
  Rect body = SubpageLayout::contentRect(safeArea, metrics);
  body.height = std::max(0, body.height - bottomInset);
  if (firstPage) {
    const int versionTop = safeArea.y + metrics.topPadding + metrics.headerHeight;
    const int notesTop = versionTop + metrics.tabBarHeight * 2 + SubpageLayout::sectionGap(metrics);
    const int bottom = body.y + body.height;
    body.y = std::min(notesTop, bottom);
    body.height = std::max(0, bottom - body.y);
  }
  return SubpageLayout::insetHorizontal(body, metrics.contentSidePadding);
}

const char* latestVersionLabel(const OtaUpdater& updater) {
#ifdef SIMULATOR
  (void)updater;
  return "nightly-ota-notes";
#else
  return updater.getLatestVersion().c_str();
#endif
}

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
    state = State::CheckingForUpdate;
  }
  requestUpdateAndWait();

#ifdef SIMULATOR
  updater.loadSimulatorReleaseNotes();
#else
  // The checking screen can reload fonts after WiFi startup cleanup, so release them again immediately before TLS.
  NetworkStartup::prepare(renderer);
  LOG_INF("OTA", "TLS preflight (manifest): free=%u, maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  const auto res = updater.checkForUpdate(selectedChannel);
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::UNSUPPORTED_CHANNEL ? tr(STR_OTA_CHANNEL_UNSUPPORTED) : nullptr;
      state = State::Failed;
    }
    requestUpdate();
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = State::NoUpdate;
    }
    requestUpdate();
    return;
  }
#endif

  {
    RenderLock lock(*this);
    releaseNotePage = 0;
    state = State::UpdateAvailable;
  }
  requestUpdate();
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  resetUi();
  failedDetail = nullptr;
  app.on(ACTION_RELEASE_PAGE, &OtaUpdateActivity::onReleasePage, this);
  app.on(ACTION_INSTALL_UPDATE, &OtaUpdateActivity::onInstallUpdate, this);
  app.setScreen(&OtaUpdateActivity::updateScreen, this);
  state = State::Ready;
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
#ifdef SIMULATOR
  onWifiSelectionComplete(true);
  return;
#endif
  // ActivityManager owns the child across frames, so stack/static lifetime is invalid.
  auto wifiSelection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiSelection) {
    LOG_ERR("OTA", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    state = State::Failed;
    requestUpdate();
    return;
  }

  // halClock.update() runs before ActivityManager::loop(); disable auto-sync before WiFi can start and race TLS.
  halClock.setAutoSyncEnabled(false);

  {
    RenderLock lock(*this);
    state = State::WifiSelection;
  }

  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::move(wifiSelection),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the ShuttingDown state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::showUpdateConfirmation() {
  static constexpr StrId OPTIONS[] = {StrId::STR_CANCEL, StrId::STR_UPDATE};
  updateConfirmation.show(StrId::STR_NEW_UPDATE, OPTIONS, 2, 0, [this](const int index) {
    if (index == 0) {
      {
        RenderLock lock(*this);
        state = State::UpdateAvailable;
      }
      requestUpdate();
      return;
    }
    runUpdateInstall();
  });
  {
    RenderLock lock(*this);
    state = State::ConfirmingUpdate;
  }
  requestUpdate();
}

void OtaUpdateActivity::rebuildReleaseNotePages(const Rect& safeArea, const int bottomInset) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto notes = updater.getReleaseNotes();
  releaseNotePageCount = 1;
  releaseNotePageStarts.fill(0);
  if (notes.empty()) {
    releaseNotePage = 0;
    return;
  }

  fui::GfxRendererTarget target(renderer);
  target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
  const fui::TextStyle style = releaseNoteStyle();
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);
  const int bulletIndent = std::max(8, renderer.getLineHeight(UI_10_FONT_ID) / 2);

  uint8_t pageCount = 0;
  uint8_t noteIndex = 0;
  while (noteIndex < notes.size() && pageCount < ReleaseJsonParser::RELEASE_NOTE_COUNT_MAX) {
    releaseNotePageStarts[pageCount] = noteIndex;
    const Rect body = releaseNotesBody(safeArea, metrics, pageCount == 0, bottomInset);
    const int availableHeight = std::max(0, body.height - titleHeight - sectionGap);
    const int textWidth = std::max(1, body.width - bulletIndent);
    int usedHeight = 0;
    const uint8_t pageStart = noteIndex;
    while (noteIndex < notes.size()) {
      const int noteHeight = fui::measureWrappedText(target, notes[noteIndex].data(), style, textWidth).height;
      const int itemHeight = noteHeight + (noteIndex == pageStart ? 0 : relatedGap);
      if (noteIndex != pageStart && usedHeight + itemHeight > availableHeight) break;
      usedHeight += itemHeight;
      ++noteIndex;
      if (usedHeight >= availableHeight) break;
    }
    ++pageCount;
  }
  releaseNotePageCount = std::max<uint8_t>(1, pageCount);
  releaseNotePageStarts[releaseNotePageCount] = static_cast<uint8_t>(notes.size());
  if (releaseNotePage >= releaseNotePageCount) releaseNotePage = releaseNotePageCount - 1;
}

void OtaUpdateActivity::renderUpdateAvailable(const Rect& safeArea) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottomInset = mappedInput.hasTouch() ? app.theme().footerHeight : 0;
  rebuildReleaseNotePages(safeArea, bottomInset);

  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_UPDATE));

  if (releaseNotePage == 0) {
    const int versionTop = safeArea.y + metrics.topPadding + metrics.headerHeight;
    GUI.drawSubHeader(renderer, Rect{safeArea.x, versionTop, safeArea.width, metrics.tabBarHeight},
                      tr(STR_CURRENT_VERSION), CROSSPOINT_VERSION);
    GUI.drawSubHeader(renderer,
                      Rect{safeArea.x, versionTop + metrics.tabBarHeight, safeArea.width, metrics.tabBarHeight},
                      tr(STR_NEW_VERSION), latestVersionLabel(updater));
  }

  const Rect body = releaseNotesBody(safeArea, metrics, releaseNotePage == 0, bottomInset);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);
  renderer.drawText(UI_12_FONT_ID, body.x, body.y, tr(STR_WHATS_NEW), true, EpdFontFamily::BOLD);
  char pageLabel[16];
  snprintf(pageLabel, sizeof(pageLabel), "%u/%u", static_cast<unsigned>(releaseNotePage + 1),
           static_cast<unsigned>(releaseNotePageCount));
  const int pageLabelWidth = renderer.getTextAdvanceX(SMALL_FONT_ID, pageLabel, EpdFontFamily::REGULAR);
  const int pageLabelY = body.y + (titleHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2;
  renderer.drawText(SMALL_FONT_ID, body.x + body.width - pageLabelWidth, pageLabelY, pageLabel);

  const auto notes = updater.getReleaseNotes();
  if (notes.empty()) {
    UITheme::drawCenteredText(renderer, body, UI_10_FONT_ID, body.y + titleHeight + sectionGap,
                              tr(STR_NO_RELEASE_NOTES));
  } else {
    fui::GfxRendererTarget target(renderer);
    target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_10_FONT_ID);
    const fui::TextStyle style = releaseNoteStyle();
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int bulletSize = std::max(3, lineHeight / 5);
    const int bulletIndent = std::max(8, lineHeight / 2);
    const int textWidth = std::max(1, body.width - bulletIndent);
    int y = body.y + titleHeight + sectionGap;
    const uint8_t start = releaseNotePageStarts[releaseNotePage];
    const uint8_t end = releaseNotePageStarts[releaseNotePage + 1];
    GfxRenderer::ClipScope clip(renderer, body.x, body.y, body.width, body.height);
    for (uint8_t i = start; i < end; ++i) {
      const int noteHeight = fui::measureWrappedText(target, notes[i].data(), style, textWidth).height;
      renderer.fillRect(body.x, y + std::max(0, (lineHeight - bulletSize) / 2), bulletSize, bulletSize, true);
      fui::layoutText(target,
                      fui::Rect{static_cast<int16_t>(body.x + bulletIndent), static_cast<int16_t>(y),
                                static_cast<int16_t>(textWidth), static_cast<int16_t>(noteHeight)},
                      notes[i].data(), style, [this](const char* line, const fui::Rect lineRect) {
                        renderer.drawText(UI_10_FONT_ID, lineRect.x, lineRect.y, line);
                      });
      y += noteHeight + relatedGap;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPDATE), releaseNotePage > 0 ? tr(STR_DIR_UP) : "",
                                            releaseNotePage + 1 < releaseNotePageCount ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderUi();
}

void OtaUpdateActivity::updateScreen(UiScreen& screen, void* user) {
  static_cast<OtaUpdateActivity*>(user)->buildUpdateScreen(screen);
}

void OtaUpdateActivity::buildUpdateScreen(UiScreen& screen) {
  if (!mappedInput.hasTouch() || (state != State::UpdateAvailable && state != State::ConfirmingUpdate)) return;
  const fui::FooterAction actions[] = {
      {tr(STR_PREV_PAGE), ACTION_RELEASE_PAGE, -1, fui::StateNormal, releaseNotePage > 0},
      {tr(STR_UPDATE), ACTION_INSTALL_UPDATE},
      {tr(STR_NEXT_PAGE), ACTION_RELEASE_PAGE, 1, fui::StateNormal, releaseNotePage + 1 < releaseNotePageCount},
  };
  screen.footer(actions, 3);
}

void OtaUpdateActivity::onReleasePage(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<OtaUpdateActivity*>(user);
  if (self->state != State::UpdateAvailable) return;
  const int next = static_cast<int>(self->releaseNotePage) + event.value;
  if (next < 0 || next >= self->releaseNotePageCount) return;
  self->releaseNotePage = static_cast<uint8_t>(next);
  self->requestUpdate();
}

void OtaUpdateActivity::onInstallUpdate(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<OtaUpdateActivity*>(user);
  if (self->state != State::UpdateAvailable) return;
  self->app.clearTapFlash();
  self->showUpdateConfirmation();
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int height = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  float updaterProgress = 0;
  if (state == State::UpdateInProgress) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = updater.getTotalSize() > 0
                          ? static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize())
                          : 0;
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) return;
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  renderer.clearScreen();

  switch (state) {
    case State::Ready: {
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
      GUI.drawSubHeader(renderer,
                        Rect{safeArea.x, safeArea.y + metrics.topPadding + metrics.headerHeight, safeArea.width,
                             metrics.tabBarHeight},
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
      break;
    }
    case State::CheckingForUpdate:
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_CHECKING_UPDATE));
      break;
    case State::UpdateAvailable:
    case State::ConfirmingUpdate:
      renderUpdateAvailable(safeArea);
      if (state == State::ConfirmingUpdate && updateConfirmation.processRender(renderer, mappedInput)) return;
      break;
    case State::UpdateInProgress: {
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
      const int blockHeight = titleHeight + sectionGap +
                              GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight) + relatedGap + height;
      int y = SubpageLayout::centeredTop(content, blockHeight);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
      y += titleHeight + sectionGap;
      y = GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight},
                              static_cast<int>(updaterProgress * 100), 100) +
          relatedGap;
      char progressText[48];
      snprintf(progressText, sizeof(progressText), "%zu / %zu", updater.getProcessedSize(), updater.getTotalSize());
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, progressText);
      break;
    }
    case State::NoUpdate: {
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case State::Failed: {
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
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
      break;
    }
    case State::Finished: {
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     tr(STR_UPDATE));
      const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + height);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true,
                                EpdFontFamily::BOLD);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap,
                                tr(STR_AUTO_RESTART_HINT));
      break;
    }
    case State::WifiSelection:
    case State::ShuttingDown:
      break;
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = State::UpdateInProgress;
  }
  requestUpdateAndWait();
  // The progress screen is another font-loading boundary before a separate TLS connection.
  NetworkStartup::prepare(renderer);
#ifndef SIMULATOR
  LOG_INF("OTA", "TLS preflight (firmware): free=%u, maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
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
      state = State::Failed;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::Finished;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = State::ShuttingDown;
  }
}

void OtaUpdateActivity::loop() {
  switch (state) {
    case State::Ready: {
      if (waitForConfirmRelease) {
        if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease = false;
        return;
      }

      const Rect readyList = getReadyListRect(renderer);
      const auto touch = handleListTouch(selectedReadyRow, READY_ROW_COUNT, readyList.y, readyList.height, false);
      if (touch == ListTouchResult::Activated) {
        activateReadyRow();
        return;
      }
      if (touch == ListTouchResult::Consumed) return;

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
    case State::UpdateAvailable: {
      const auto touch = routeTouch(mappedInput);
      if (touch.routed && app.invalidated()) requestUpdate();
      if (touch) return;
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        showUpdateConfirmation();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious) && releaseNotePage > 0) {
        --releaseNotePage;
        requestUpdate();
        return;
      }
      if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) && releaseNotePage + 1 < releaseNotePageCount) {
        ++releaseNotePage;
        requestUpdate();
      }
      return;
    }
    case State::ConfirmingUpdate:
      if (updateConfirmation.handleInput(mappedInput, [this] { requestUpdate(); })) {
        if (state == State::ConfirmingUpdate && !updateConfirmation.isActive()) {
          state = State::UpdateAvailable;
          requestUpdate();
        }
        return;
      }
      state = State::UpdateAvailable;
      requestUpdate();
      return;
    case State::Failed:
    case State::NoUpdate: {
      int x = 0;
      int y = 0;
      if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) finish();
      return;
    }
    case State::ShuttingDown:
      ESP.restart();
      return;
    case State::WifiSelection:
    case State::CheckingForUpdate:
    case State::UpdateInProgress:
    case State::Finished:
      return;
  }
}
