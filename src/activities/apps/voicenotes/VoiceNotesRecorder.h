#pragma once

#include <HalStorage.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Streams the microphone to a 16 kHz mono PCM16 WAV on SD from its own task, so
// e-ink refreshes and the main loop never stall capture. The header sizes are
// rewritten every few seconds: a power loss costs at most that much audio.
class VoiceNotesRecorder {
 public:
  VoiceNotesRecorder() = default;
  ~VoiceNotesRecorder() { stop(); }
  VoiceNotesRecorder(const VoiceNotesRecorder&) = delete;
  VoiceNotesRecorder& operator=(const VoiceNotesRecorder&) = delete;

  // Main task only. Powers the mic, creates the file, and starts the task.
  bool start(const char* wavPath);
  // Main task only; idempotent. Joins the task, finalizes the header, and
  // releases the mic.
  void stop();

  // True from start() until the task exits (stop, time limit, or error).
  bool capturing() const { return task_ != nullptr && !taskDone_.load(); }
  bool failed() const { return failed_.load(); }
  bool limitReached() const { return limitReached_.load(); }
  uint32_t elapsedSeconds() const;

 private:
  static void taskEntry(void* self);
  void run();
  bool writeSamples(size_t count);
  bool patchHeader();

  // 2048 samples = 128 ms per SD write; lives inside the (heap-allocated)
  // owning Activity, so no separate allocation.
  static constexpr size_t BUFFER_SAMPLES = 2048;

  HalFile file_;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> taskDone_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> limitReached_{false};
  std::atomic<uint32_t> samplesWritten_{0};
  int16_t buffer_[BUFFER_SAMPLES] = {};
};
