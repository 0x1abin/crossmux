#include "ClockSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

void ClockSyncActivity::onEnter() {
  Activity::onEnter();
  state = State::Syncing;
  syncedTime[0] = '\0';

  if (WiFi.status() == WL_CONNECTED) {
    NetworkStartup::prepare(renderer);
    requestUpdate();
    return;
  }

  shouldTearDownWifiOnExit = true;
  launchWifiSelection();
}

void ClockSyncActivity::onExit() {
  Activity::onExit();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ClockSyncActivity::launchWifiSelection() {
  LOG_INF("CLK", "Manual sync requested without WiFi, launching WiFi selection");
  // ActivityManager owns the picker across frames; stack lifetime is insufficient.
  auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("CLK", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    state = State::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ClockSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    LOG_INF("CLK", "WiFi selection cancelled before manual clock sync");
    finish();
    return;
  }

  state = State::Syncing;
  requestUpdate();
}

void ClockSyncActivity::runSync() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_INF("CLK", "Manual sync requested but WiFi is not connected after selection");
    state = State::NoWifi;
    requestUpdate();
    return;
  }

  const bool ok = halClock.syncNow();
  if (!ok) {
    state = State::Failed;
    requestUpdate();
    return;
  }

  char buf[9];
  if (TimeUtils::formatCurrentTime(buf, sizeof(buf), SETTINGS.clockFormat == 1)) {
    snprintf(syncedTime, sizeof(syncedTime), "%s", buf);
  }
  state = State::Success;
  requestUpdate();
}

void ClockSyncActivity::loop() {
  if (state == State::Syncing) {
    // First-tick: render the "Syncing..." screen, then perform the (blocking) sync.
    // requestUpdateAndWait below forces the render before we block on WiFi.
    requestUpdateAndWait();
    runSync();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void ClockSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_CLOCK_SYNC));
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int detailHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);

  switch (state) {
    case State::Syncing:
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_CLOCK_SYNCING));
      break;
    case State::Success: {
      const int top =
          SubpageLayout::centeredTop(content, titleHeight + (syncedTime[0] != '\0' ? relatedGap + detailHeight : 0));
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top, tr(STR_CLOCK_SYNC_OK), true,
                                EpdFontFamily::BOLD);
      if (syncedTime[0] != '\0') {
        // Sized for the label in any language: STR_CURRENT_TIME is 26 bytes in
        // Russian (UTF-8 Cyrillic is 2 bytes per letter) versus 13 in English,
        // plus a separator and up to "08:56 PM".
        char line[64];
        snprintf(line, sizeof(line), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
        UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap, line);
      }
      break;
    }
    case State::NoWifi: {
      const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + detailHeight);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top, tr(STR_CLOCK_SYNC_NO_WIFI), true,
                                EpdFontFamily::BOLD);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap,
                                tr(STR_CLOCK_SYNC_NO_WIFI_HINT));
    } break;
    case State::Failed: {
      const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + detailHeight);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top, tr(STR_CLOCK_SYNC_FAIL), true,
                                EpdFontFamily::BOLD);
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap,
                                tr(STR_CHECK_SERIAL_OUTPUT));
    } break;
  }

  if (state != State::Syncing) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
