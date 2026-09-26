#include "VoiceNotesPlayActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>

#include "VoiceNotesPlayer.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// E-ink cannot redraw every second; a fast refresh every 5 s keeps the clock
// honest without constant flashing (same cadence as recording).
constexpr uint32_t REFRESH_STEP_SECONDS = 5;
constexpr int SKIP_SECONDS = 10;
// Same hold threshold the reader uses to tell a chapter skip from a page turn.
constexpr unsigned long VOLUME_HOLD_MS = 700;

void formatClock(char* out, const size_t outLen, const uint32_t seconds) {
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
}
}  // namespace

VoiceNotesPlayActivity::VoiceNotesPlayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const char* recordingName)
    : Activity("VoiceNotesPlay", renderer, mappedInput) {
  snprintf(name_, sizeof(name_), "%s", recordingName);
}

void VoiceNotesPlayActivity::onEnter() {
  Activity::onEnter();
  state_ = State::Error;
  shownSeconds_ = 0;
  durationSeconds_ = 0;
  pressSeen_ = false;

  char path[voicenotes::PATH_LEN];
  voicenotes::wavPath(name_, path, sizeof(path));
  if (voicenotes::player::open(path)) {
    durationSeconds_ = voicenotes::player::durationSeconds();
    startAt(0);
  }
  requestUpdate();
}

void VoiceNotesPlayActivity::onExit() {
  popup_.dismiss();
  // Sleep or any other exit path must release the speaker and the file.
  voicenotes::player::close();
  Activity::onExit();
}

void VoiceNotesPlayActivity::startAt(const uint32_t second) {
  if (voicenotes::player::play(second)) {
    state_ = State::Playing;
    shownSeconds_ = second < durationSeconds_ ? second : 0;
  } else {
    voicenotes::player::close();
    state_ = State::Error;
  }
}

void VoiceNotesPlayActivity::pause() {
  voicenotes::player::pause();
  state_ = State::Paused;
  shownSeconds_ = voicenotes::player::positionSeconds();
}

void VoiceNotesPlayActivity::skip(const int seconds) {
  // While paused the player position is stale; shownSeconds_ is the resume point.
  const uint32_t from = state_ == State::Playing ? voicenotes::player::positionSeconds() : shownSeconds_;
  const int64_t target = static_cast<int64_t>(from) + seconds;
  uint32_t second = target < 0 ? 0 : static_cast<uint32_t>(target);
  if (second >= durationSeconds_) second = durationSeconds_ > 0 ? durationSeconds_ - 1 : 0;
  if (state_ == State::Playing) {
    startAt(second);
  } else {
    // Paused: move the resume point without waking the speaker.
    shownSeconds_ = second;
  }
}

void VoiceNotesPlayActivity::showVolumeMenu() {
  const StrId options[] = {StrId::STR_SOUND_FEEDBACK_LOW, StrId::STR_SOUND_FEEDBACK_MEDIUM,
                           StrId::STR_SOUND_FEEDBACK_HIGH};
  popup_.show(StrId::STR_VOICE_NOTES_VOLUME, options, 3, static_cast<int>(voicenotes::player::volume()),
              [](const int choice) {
                if (choice < 0 || choice > static_cast<int>(voicenotes::player::Volume::High)) return;
                voicenotes::player::setVolume(static_cast<voicenotes::player::Volume>(choice));
              });
  requestUpdate();
}

void VoiceNotesPlayActivity::loop() {
  using Button = MappedInputManager::Button;

  if (state_ == State::Playing && !voicenotes::player::playing()) {
    // Reached the end (or the source failed): stay on screen, paused at the
    // start so Confirm replays.
    {
      RenderLock lock(*this);
      voicenotes::player::pause();
      state_ = State::Paused;
      shownSeconds_ = 0;
    }
    requestUpdate();
    return;
  }

  // The clock is not refreshed under the popup; resync it when the popup closes.
  if (popup_.handleInput(mappedInput, [this] {
        if (state_ == State::Playing) shownSeconds_ = voicenotes::player::positionSeconds();
        requestUpdate();
      })) {
    return;
  }

  if (mappedInput.wasPressed(Button::Confirm) || mappedInput.wasPressed(Button::Back) ||
      mappedInput.wasPressed(Button::NavPrevious) || mappedInput.wasPressed(Button::NavNext)) {
    pressSeen_ = true;
  }
  if (pressSeen_) {
    // A hold fires while the button is still down and swallows its release, so
    // it never also skips.
    if (state_ != State::Error && (mappedInput.wasLongPressed(Button::NavPrevious, VOLUME_HOLD_MS) ||
                                   mappedInput.wasLongPressed(Button::NavNext, VOLUME_HOLD_MS))) {
      showVolumeMenu();
      return;
    }
    if (mappedInput.wasReleased(Button::Back) ||
        (state_ == State::Error && mappedInput.wasReleased(Button::Confirm))) {
      finish();
      return;
    }
    if (state_ != State::Error && mappedInput.wasReleased(Button::Confirm)) {
      {
        RenderLock lock(*this);
        if (state_ == State::Playing) {
          pause();
        } else {
          startAt(shownSeconds_);
        }
      }
      requestUpdate();
      return;
    }
    if (state_ != State::Error &&
        (mappedInput.wasReleased(Button::NavPrevious) || mappedInput.wasReleased(Button::NavNext))) {
      {
        RenderLock lock(*this);
        skip(mappedInput.wasReleased(Button::NavPrevious) ? -SKIP_SECONDS : SKIP_SECONDS);
      }
      requestUpdate();
      return;
    }
  }

  if (state_ == State::Playing) {
    const uint32_t seconds = voicenotes::player::positionSeconds();
    if (seconds / REFRESH_STEP_SECONDS != shownSeconds_ / REFRESH_STEP_SECONDS) {
      shownSeconds_ = seconds;
      requestUpdate();
    }
  }
}

void VoiceNotesPlayActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 name_);

  if (state_ == State::Error) {
    UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, content.y, textBounds.width, content.height},
                                     UI_10_FONT_ID, tr(STR_VOICE_NOTES_PLAY_FAILED), 3);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  char clock[16];
  formatClock(clock, sizeof(clock), shownSeconds_);
  char total[16];
  formatClock(total, sizeof(total), durationSeconds_);
  char clockLine[40];
  snprintf(clockLine, sizeof(clockLine), "%s / %s", clock, total);

  const bool playing = state_ == State::Playing;
  int y = SubpageLayout::centeredTop(content, titleHeight + sectionGap + titleHeight);
  UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y,
                            playing ? tr(STR_VOICE_NOTES_PLAYING) : tr(STR_VOICE_NOTES_PAUSED), true,
                            EpdFontFamily::BOLD);
  y += titleHeight + sectionGap;
  UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, clockLine);

  if (popup_.processRender(renderer, mappedInput)) return;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), playing ? tr(STR_VOICE_NOTES_PAUSE) : tr(STR_VOICE_NOTES_PLAY),
                            tr(STR_VOICE_NOTES_SKIP_BACK), tr(STR_VOICE_NOTES_SKIP_FORWARD));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
