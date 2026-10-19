#include "UsbDriveActivity.h"

#if FREEINK_CAP_USB_MSC

#include <Arduino.h>
#include <HalStorage.h>
#include <I18n.h>

#include "BleInput.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void UsbDriveActivity::onEnter() {
  bleinput::stop();
  Activity::onEnter();
  resetUi();
  app.setScreen(&UsbDriveActivity::driveScreen, this);

  // Show the safety instructions before giving the raw SD card to the USB host.
  requestUpdateAndWait();
  if (!Storage.beginUsbDrive()) {
    LOG_ERR("USB", "Unable to start USB Drive");
    preparing = false;
    startFailed = true;
    state = State::IoError;
    startFailureStartedAt = millis();
    requestUpdate();
    return;
  }

  preparing = false;
  state = State::WaitingForHost;
  hostWaitStartedAt = millis();
  requestUpdate();
}

void UsbDriveActivity::onExit() {
  if (!restartRequested) Storage.endUsbDrive();
  Activity::onExit();
}

void UsbDriveActivity::loop() {
  if (!startFailed) {
    const State nextState = Storage.usbDriveState();
    if (nextState != state) {
      state = nextState;
      requestUpdate();
    }
  }

  // A battery-powered USB device may stay mounted after cable removal. End
  // sustained suspend sessions too; host sleep intentionally has the same policy.
  if (state == State::Connected && Storage.usbDriveHostSuspended()) {
    if (!hostSuspendPending) {
      hostSuspendPending = true;
      hostSuspendStartedAt = millis();
    } else if (millis() - hostSuspendStartedAt >= HOST_SUSPEND_TIMEOUT_MS) {
      LOG_INF("USB", "USB Drive host suspend timed out; ending session");
      // endUsbDrive() soft-disconnects before teardown and reboot.
      restartToHome();
      return;
    }
  } else {
    hostSuspendPending = false;
  }

  if (state == State::WaitingForHost && millis() - hostWaitStartedAt >= HOST_WAIT_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive host wait timed out");
    restartToHome();
    return;
  }

  if (startFailed && millis() - startFailureStartedAt >= START_FAILURE_TIMEOUT_MS) {
    LOG_INF("USB", "USB Drive startup failure timed out");
    restartToHome();
    return;
  }

  if (!startFailed && state == State::IoError) {
    if (!forcedDisconnectRequested) {
      forcedDisconnectRequested = true;
      forcedDisconnectRequestedAt = millis();
      LOG_ERR("USB", "USB Drive I/O error; disconnecting host");
      if (!Storage.disconnectUsbDriveHost()) {
        LOG_ERR("USB", "Unable to request USB Drive host disconnect");
      }
    } else if (millis() - forcedDisconnectRequestedAt >= FORCED_DISCONNECT_TIMEOUT_MS) {
      LOG_ERR("USB", "USB Drive host disconnect timed out; forcing restart");
      restartToHome();
    }
    return;
  }

  const bool canExitWithInput = state == State::WaitingForHost || startFailed;
  if (canExitWithInput && (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Power) || mappedInput.wasHomeGesture())) {
    restartToHome();
    return;
  }

  if (state == State::Ejected || state == State::Disconnected || state == State::Unsupported) {
    restartToHome();
  }
}

void UsbDriveActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_USB_DRIVE));

  renderUi();

  if (state == State::WaitingForHost || startFailed) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}

void UsbDriveActivity::driveScreen(UiScreen& screen, void* user) {
  static_cast<UsbDriveActivity*>(user)->buildDriveScreen(screen);
}

void UsbDriveActivity::buildDriveScreen(UiScreen& screen) const {
  const char* message = nullptr;
  const char* detail = nullptr;
  const char* secondaryDetail = nullptr;
  if (preparing) {
    message = tr(STR_USB_DRIVE_PREPARING);
    detail = tr(STR_USB_DRIVE_EJECT_HINT);
  } else {
    switch (state) {
      case State::WaitingForHost:
        message = tr(STR_USB_DRIVE_WAITING);
        break;
      case State::Connected:
        message = tr(STR_USB_DRIVE_CONNECTED);
        detail = tr(STR_USB_DRIVE_CONNECT_DELAY);
        secondaryDetail = tr(STR_USB_DRIVE_EJECT_HINT);
        break;
      case State::IoError:
        message = startFailed ? tr(STR_USB_DRIVE_START_ERROR) : tr(STR_USB_DRIVE_ERROR);
        break;
      case State::Ejected:
      case State::Disconnected:
      case State::Unsupported:
        return;
    }
  }

  // Draw the status text through the GfxRenderer with the built-in CJK
  // NotoSans subset instead of the FreeInkUI text component. The UI fonts
  // (Ubuntu UI / Noto Sans 8px) only cover Latin and Hebrew; CJK glyphs are
  // served at runtime from a TTF on the SD card (see UIScale.h), and
  // beginUsbDrive() detaches the SD filesystem for the USB host, so that
  // fallback is unavailable here and FreeInkUI text renders as empty boxes.
  // The built-in CJK font lives in flash and needs no SD access.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const Rect body{metrics.contentSidePadding, metrics.topPadding + metrics.headerHeight,
                  pageWidth - 2 * metrics.contentSidePadding,
                  pageHeight - metrics.topPadding - metrics.headerHeight - metrics.buttonHintsHeight};

  // notosans_cjk_* ship in flash without a bold variant, so the message line
  // is drawn in REGULAR here; the original bold flag was applied through the
  // SD-card CJK fallback that is unavailable while the USB host owns the card.
  constexpr int fontId = NOTOSANS_12_FONT_ID;
  constexpr EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  // The flash CJK face covers CJK and Latin but has no Hebrew, and the SD-backed
  // fallback that used to supply other scripts is gone for the duration of the
  // USB session. Point its fallback at the flash UI face, which does cover
  // Hebrew, so a Hebrew UI renders the status text instead of empty boxes.
  // A run switches to the fallback only when the primary lacks one of its
  // codepoints, so Latin and CJK text keep the CJK face. Restored below: no
  // other screen may inherit this registration.
  renderer.setFallbackFont(fontId, UI_12_FONT_ID);
  const int lineHeight = renderer.getLineHeight(fontId);
  constexpr int gap = UiHighDpiProfile::controlGap;  // equals FreeInkUI spaceMd (UIThemeTokens.h)

  const auto lines = [&](const char* text, const int maxLines) {
    if (!text) return std::vector<std::string>{};
    return renderer.wrappedText(fontId, text, body.width, maxLines, style);
  };
  const auto messageLines = lines(message, 2);
  const auto detailLines = lines(detail, 3);
  const auto secondaryLines = lines(secondaryDetail, 3);
  const int messageHeight = static_cast<int>(messageLines.size()) * lineHeight;
  const int detailHeight = static_cast<int>(detailLines.size()) * lineHeight;
  const int secondaryHeight = static_cast<int>(secondaryLines.size()) * lineHeight;
  const int totalHeight =
      messageHeight + (detail ? gap : 0) + detailHeight + (secondaryDetail ? gap : 0) + secondaryHeight;

  // Center vertically only when the content fits; otherwise start at the body
  // top (mirrors the original spacer guard) so lines never draw above the header.
  int y = body.y + (body.height > totalHeight ? (body.height - totalHeight) / 2 : 0);
  const auto drawLines = [&](const std::vector<std::string>& textLines) {
    for (const auto& line : textLines) {
      UITheme::drawCenteredText(renderer, body, fontId, y, line.c_str(), true, style);
      y += lineHeight;
    }
  };
  drawLines(messageLines);
  y += detail ? gap : 0;
  drawLines(detailLines);
  y += secondaryDetail ? gap : 0;
  drawLines(secondaryLines);
  // Undo the script fallback: this screen is the only place that wants the CJK
  // face to reach Hebrew, and leaving it registered would change how every
  // other screen resolves missing glyphs.
  renderer.setFallbackFont(fontId, 0, 0);
}

void UsbDriveActivity::restartToHome() {
  if (restartRequested) return;
  restartRequested = true;
  Storage.endUsbDrive();
  delay(20);
  restartToHomeAfterStorageHandoff();
}

#endif  // FREEINK_CAP_USB_MSC
