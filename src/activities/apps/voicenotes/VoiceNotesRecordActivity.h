#pragma once

#include <I18n.h>

#include <cstdint>

#include "VoiceNotesRecorder.h"
#include "VoiceNotesStore.h"
#include "activities/Activity.h"

// Child of VoiceNotesActivity: records until Stop, the time limit, or an error,
// then finishes back to the list.
class VoiceNotesRecordActivity final : public Activity {
 public:
  explicit VoiceNotesRecordActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("VoiceNotesRecord", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Recording; }

 private:
  enum class State : uint8_t { Recording, Stopped };

  void stopRecording();

  VoiceNotesRecorder recorder_;
  State state_ = State::Stopped;
  // Message shown after capture ends early; STR_NONE_OPT means none.
  StrId notice_ = StrId::STR_NONE_OPT;
  char wavPath_[voicenotes::PATH_LEN] = {};
  uint32_t shownSeconds_ = 0;
  // One gesture, one action: a button still held from the list screen must be
  // released before a press here counts.
  bool pressSeen_ = false;
};
