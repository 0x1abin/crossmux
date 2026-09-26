#pragma once

#include <I18n.h>

#include <cstdint>

#include "VoiceNotesStore.h"
#include "activities/Activity.h"

// Child of VoiceNotesActivity: plays one recording through the speaker.
// Confirm pauses/resumes, previous/next skip 10 s, Back stops and returns to
// the list.
class VoiceNotesPlayActivity final : public Activity {
 public:
  VoiceNotesPlayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* recordingName);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Playing; }

 private:
  enum class State : uint8_t { Playing, Paused, Error };

  void startAt(uint32_t second);
  void pause();
  void skip(int seconds);

  char name_[voicenotes::NAME_LEN] = {};
  State state_ = State::Error;
  uint32_t durationSeconds_ = 0;
  uint32_t shownSeconds_ = 0;
  // One gesture, one action: a button still held from the list's popup must be
  // released before a press here counts.
  bool pressSeen_ = false;
};
