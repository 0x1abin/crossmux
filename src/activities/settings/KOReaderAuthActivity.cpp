#include "KOReaderAuthActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderSyncClient.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

void KOReaderAuthActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  WiFi.setSleep(false);
  LOG_DBG("KOAuth", "WiFi sleep disabled for authentication");

  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = mode == Mode::SIGN_UP ? tr(STR_CREATING_ACCOUNT) : tr(STR_AUTHENTICATING);
  }
  requestUpdate();

  performAuthentication();
}

void KOReaderAuthActivity::performAuthentication() {
  const auto result = mode == Mode::SIGN_UP ? KOReaderSyncClient::createUser() : KOReaderSyncClient::authenticate();

  {
    RenderLock lock(*this);
    if (result == KOReaderSyncClient::OK) {
      state = SUCCESS;
      statusMessage = mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS);
    } else {
      state = FAILED;
      errorMessage =
          result == KOReaderSyncClient::USER_EXISTS ? tr(STR_USERNAME_TAKEN) : KOReaderSyncClient::errorString(result);
    }
  }
  requestUpdate();
}

void KOReaderAuthActivity::onEnter() {
  Activity::onEnter();

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    NetworkStartup::prepare(renderer);
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  if (!startActivityForResultWith<WifiSelectionActivity>(
          [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); })) {
    state = FAILED;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
  }
}

void KOReaderAuthActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void KOReaderAuthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 mode == Mode::SIGN_UP ? tr(STR_SIGN_UP) : tr(STR_KOREADER_AUTH));
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);

  if (state == AUTHENTICATING) {
    UITheme::drawCenteredWrappedText(renderer, textBounds, UI_10_FONT_ID, statusMessage.c_str(), 2);
  } else if (state == SUCCESS) {
    const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + lineHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top,
                              mode == Mode::SIGN_UP ? tr(STR_ACCOUNT_CREATED) : tr(STR_AUTH_SUCCESS), true,
                              EpdFontFamily::BOLD);
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, top + titleHeight + relatedGap, tr(STR_SYNC_READY));
  } else if (state == FAILED) {
    const int top = SubpageLayout::centeredTop(content, titleHeight + relatedGap + lineHeight * 2);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, top,
                              mode == Mode::SIGN_UP ? tr(STR_SIGNUP_FAILED) : tr(STR_AUTH_FAILED), true,
                              EpdFontFamily::BOLD);
    UITheme::drawCenteredWrappedText(
        renderer, Rect{textBounds.x, top + titleHeight + relatedGap, textBounds.width, lineHeight * 2}, UI_10_FONT_ID,
        errorMessage.c_str(), 2, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void KOReaderAuthActivity::loop() {
  if (state == SUCCESS || state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
  }
}
