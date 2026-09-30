#include "VoiceNotesTranscribeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <OpenAiCredentialStore.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "NetworkStartup.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Progress repaints are fast refreshes; every few seconds is enough feedback
// for a multi-megabyte upload without turning the panel into a strobe.
constexpr uint32_t PROGRESS_REFRESH_MS = 3000;
}  // namespace

VoiceNotesTranscribeActivity::VoiceNotesTranscribeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const char* recordingName)
    : Activity("VoiceNotesTranscribe", renderer, mappedInput) {
  snprintf(name_, sizeof(name_), "%s", recordingName);
  voicenotes::wavPath(name_, wavPath_, sizeof(wavPath_));
  voicenotes::txtPath(name_, txtPath_, sizeof(txtPath_));
}

void VoiceNotesTranscribeActivity::onEnter() {
  Activity::onEnter();
  state_ = State::WaitingForWifi;
  ownsWifi_ = false;
  uploadQueued_ = false;
  pressSeen_ = false;
  sentBytes_.store(0);
  totalBytes_.store(0);

  if (!OPENAI_STORE.hasApiKey()) {
    failureTitle_ = tr(STR_VOICE_NOTES_FAILED);
    snprintf(detail_, sizeof(detail_), "%s", tr(STR_VOICE_NOTES_NO_KEY));
    state_ = State::Failed;
    requestUpdate();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    NetworkStartup::prepare(renderer);
    uploadQueued_ = true;
    requestUpdate();
    return;
  }

  ownsWifi_ = true;
  requestUpdate();
  if (!startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
        if (!result.isCancelled && WiFi.status() == WL_CONNECTED) {
          uploadQueued_ = true;
          return;
        }
        failureTitle_ = tr(STR_VOICE_NOTES_FAILED);
        snprintf(detail_, sizeof(detail_), "%s", tr(STR_VOICE_NOTES_WIFI_FAILED));
        state_ = State::Failed;
      })) {
    failureTitle_ = tr(STR_VOICE_NOTES_FAILED);
    snprintf(detail_, sizeof(detail_), "%s", tr(STR_MEMORY_ERROR));
    state_ = State::Failed;
  }
}

void VoiceNotesTranscribeActivity::onExit() {
  if (ownsWifi_ && WiFi.getMode() != WIFI_MODE_NULL) {
    // Same teardown as AirPage. S3 targets keep their heap in PSRAM, so no
    // silent restart is needed to undo Wi-Fi fragmentation.
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    esp_wifi_deinit();
  }
  ownsWifi_ = false;
  Activity::onExit();
}

void VoiceNotesTranscribeActivity::showFailure(const char* detail) {
  RenderLock lock(*this);
  failureTitle_ = tr(STR_VOICE_NOTES_FAILED);
  snprintf(detail_, sizeof(detail_), "%s", detail ? detail : "");
  state_ = State::Failed;
}

bool VoiceNotesTranscribeActivity::onProgress(const size_t sent, const size_t total) {
  sentBytes_.store(static_cast<uint32_t>(sent));
  totalBytes_.store(static_cast<uint32_t>(total));
  // The upload blocks loop(), so poll input here. Cancel on Back's release so
  // that release is consumed here and cannot also close the list screen.
  mappedInput.update();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return false;
  const uint32_t now = millis();
  if (now - lastRefreshMs_ >= PROGRESS_REFRESH_MS) {
    lastRefreshMs_ = now;
    requestUpdate(true);
  }
  return true;
}

void VoiceNotesTranscribeActivity::startUpload() {
  {
    RenderLock lock(*this);
    state_ = State::Uploading;
  }
  requestUpdateAndWait();
  lastRefreshMs_ = millis();

  LOG_INF("VNT", "Transcribing %s", wavPath_);
  const OpenAiTranscriber::Result result =
      OpenAiTranscriber::transcribe(OPENAI_STORE.getApiKey().c_str(), wavPath_, txtPath_,
                                    [this](const size_t sent, const size_t total) { return onProgress(sent, total); });

  // Only a press that starts after the upload may close the result screen.
  pressSeen_ = false;
  using Status = OpenAiTranscriber::Status;
  switch (result.status) {
    case Status::Ok: {
      RenderLock lock(*this);
      state_ = State::Done;
      break;
    }
    case Status::Cancelled:
      LOG_INF("VNT", "Cancelled by user");
      finish();
      return;
    case Status::NoApiKey:
      showFailure(tr(STR_VOICE_NOTES_NO_KEY));
      break;
    case Status::FileTooLarge:
      showFailure(tr(STR_VOICE_NOTES_TOO_LARGE));
      break;
    case Status::LowMemory:
      showFailure(tr(STR_MEMORY_ERROR));
      break;
    default:
      LOG_ERR("VNT", "Failed: status=%u http=%d %s", static_cast<unsigned>(result.status), result.httpStatus,
              result.message);
      showFailure(result.message);
      break;
  }
  requestUpdate();
}

void VoiceNotesTranscribeActivity::loop() {
  if (uploadQueued_) {
    uploadQueued_ = false;
    startUpload();
    return;
  }
  if (state_ != State::Done && state_ != State::Failed) return;

  using Button = MappedInputManager::Button;
  if (mappedInput.wasPressed(Button::Confirm) || mappedInput.wasPressed(Button::Back)) pressSeen_ = true;
  if (pressSeen_ && (mappedInput.wasReleased(Button::Confirm) || mappedInput.wasReleased(Button::Back))) {
    if (state_ == State::Done) {
      setResult(FilePathResult{txtPath_});
    } else {
      ActivityResult cancelled;
      cancelled.isCancelled = true;
      setResult(std::move(cancelled));
    }
    finish();
  }
}

void VoiceNotesTranscribeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int bodyHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 name_);

  switch (state_) {
    case State::WaitingForWifi:
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_CONNECTING));
      break;
    case State::Uploading: {
      const uint32_t sent = sentBytes_.load();
      const uint32_t total = totalBytes_.load();
      const bool waiting = total > 0 && sent >= total;
      const int blockHeight = titleHeight + sectionGap +
                              GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight) + relatedGap +
                              bodyHeight;
      int y = SubpageLayout::centeredTop(content, blockHeight);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y,
                                waiting ? tr(STR_VOICE_NOTES_WAITING) : tr(STR_VOICE_NOTES_UPLOADING), true,
                                EpdFontFamily::BOLD);
      y += titleHeight + sectionGap;
      y = GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight}, sent,
                              total > 0 ? total : 1) +
          relatedGap;
      char progressText[40];
      snprintf(progressText, sizeof(progressText), "%u / %u KB", static_cast<unsigned>(sent / 1024),
               static_cast<unsigned>(total / 1024));
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, progressText);
      const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case State::Done: {
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_VOICE_NOTES_DONE), true, EpdFontFamily::BOLD);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
    case State::Failed: {
      const int detailHeight = bodyHeight * 4;
      int y = SubpageLayout::centeredTop(content, titleHeight + sectionGap + detailHeight);
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y,
                                failureTitle_ ? failureTitle_ : tr(STR_VOICE_NOTES_FAILED), true, EpdFontFamily::BOLD);
      y += titleHeight + sectionGap;
      UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, y, textBounds.width, detailHeight}, UI_10_FONT_ID,
                                       detail_, 4, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }
  }
  renderer.displayBuffer();
}
