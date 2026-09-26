#pragma once

#include <OpenAiTranscriber.h>

#include <atomic>
#include <cstdint>

#include "VoiceNotesStore.h"
#include "activities/Activity.h"

// Child of VoiceNotesActivity: brings Wi-Fi up if needed, uploads one
// recording to OpenAI, and saves the transcript beside it. Wi-Fi it started is
// torn down on exit.
class VoiceNotesTranscribeActivity final : public Activity {
 public:
  VoiceNotesTranscribeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* recordingName);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == State::Uploading; }

 private:
  enum class State : uint8_t { WaitingForWifi, Uploading, Done, Failed };

  void startUpload();
  bool onProgress(size_t sent, size_t total);
  void showFailure(const char* detail);

  char name_[voicenotes::NAME_LEN] = {};
  char wavPath_[voicenotes::PATH_LEN] = {};
  char txtPath_[voicenotes::PATH_LEN] = {};
  State state_ = State::WaitingForWifi;
  bool ownsWifi_ = false;
  bool uploadQueued_ = false;
  std::atomic<uint32_t> sentBytes_{0};
  std::atomic<uint32_t> totalBytes_{0};
  uint32_t lastRefreshMs_ = 0;
  // Failure text: an OpenAI error.message or a transport detail.
  char detail_[sizeof(OpenAiTranscriber::Result::message)] = {};
  const char* failureTitle_ = nullptr;
  bool pressSeen_ = false;
};
