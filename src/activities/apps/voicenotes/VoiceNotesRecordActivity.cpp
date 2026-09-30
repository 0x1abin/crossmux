#include "VoiceNotesRecordActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>

#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// E-ink cannot redraw every second; a fast refresh every 5 s keeps the timer
// honest without constant flashing.
constexpr uint32_t REFRESH_STEP_SECONDS = 5;

void formatClock(char* out, const size_t outLen, const uint32_t seconds) {
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
}
}  // namespace

void VoiceNotesRecordActivity::onEnter() {
  Activity::onEnter();
  state_ = State::Stopped;
  notice_ = StrId::STR_NONE_OPT;
  shownSeconds_ = 0;
  pressSeen_ = false;

  if (!voicenotes::nextWavPath(wavPath_, sizeof(wavPath_))) {
    notice_ = StrId::STR_VOICE_NOTES_RECORD_FAILED;
  } else if (!recorder_.start(wavPath_)) {
    notice_ = StrId::STR_VOICE_NOTES_MIC_ERROR;
  } else {
    state_ = State::Recording;
  }
  requestUpdate();
}

void VoiceNotesRecordActivity::onExit() {
  // Sleep or any other exit path must still finalize the WAV and free the mic.
  recorder_.stop();
  Activity::onExit();
}

void VoiceNotesRecordActivity::stopRecording() {
  recorder_.stop();
  state_ = State::Stopped;
  if (recorder_.failed()) {
    notice_ = StrId::STR_VOICE_NOTES_RECORD_FAILED;
  } else if (recorder_.limitReached()) {
    notice_ = StrId::STR_VOICE_NOTES_LIMIT_REACHED;
  }
}

void VoiceNotesRecordActivity::loop() {
  using Button = MappedInputManager::Button;

  if (state_ == State::Recording && !recorder_.capturing()) {
    // The task ended on its own: time limit or write failure.
    {
      RenderLock lock(*this);
      stopRecording();
    }
    if (notice_ == StrId::STR_NONE_OPT) {
      finish();
      return;
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(Button::Confirm) || mappedInput.wasPressed(Button::Back)) pressSeen_ = true;
  const bool stopPressed =
      pressSeen_ && (mappedInput.wasReleased(Button::Confirm) || mappedInput.wasReleased(Button::Back));

  if (state_ == State::Recording) {
    if (stopPressed) {
      {
        RenderLock lock(*this);
        stopRecording();
      }
      if (notice_ == StrId::STR_NONE_OPT) {
        finish();
        return;
      }
      requestUpdate();
      return;
    }
    const uint32_t seconds = recorder_.elapsedSeconds();
    if (seconds / REFRESH_STEP_SECONDS != shownSeconds_ / REFRESH_STEP_SECONDS) {
      shownSeconds_ = seconds;
      requestUpdate();
    }
    return;
  }

  if (stopPressed) finish();
}

void VoiceNotesRecordActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_VOICE_NOTES_TITLE));

  char clock[16];
  formatClock(clock, sizeof(clock), shownSeconds_);
  char limit[16];
  formatClock(limit, sizeof(limit), voicenotes::MAX_RECORDING_SECONDS);
  char clockLine[40];
  snprintf(clockLine, sizeof(clockLine), "%s / %s", clock, limit);

  if (state_ == State::Recording) {
    int y = SubpageLayout::centeredTop(content, titleHeight + sectionGap + titleHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_VOICE_NOTES_RECORDING), true,
                              EpdFontFamily::BOLD);
    y += titleHeight + sectionGap;
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, clockLine);
    const auto labels = mappedInput.mapLabels(tr(STR_VOICE_NOTES_STOP), tr(STR_VOICE_NOTES_STOP), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, content.y, textBounds.width, content.height},
                                     UI_10_FONT_ID, notice_ == StrId::STR_NONE_OPT ? "" : I18N.get(notice_), 3);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
