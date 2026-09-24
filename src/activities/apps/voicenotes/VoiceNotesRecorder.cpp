#include "VoiceNotesRecorder.h"

#include <Arduino.h>
#include <HalMicrophone.h>
#include <Logging.h>

#include <cstring>

#include "VoiceNotesStore.h"

namespace {

// SdFat plus the log path need more than the 2 KB minimum; measured headroom
// is checked with uxTaskGetStackHighWaterMark in the stop() log line.
constexpr uint32_t TASK_STACK_BYTES = 6144;
// Above the loop and render tasks (priority 1) so SD writes keep up with I2S.
constexpr UBaseType_t TASK_PRIORITY = 5;
// Core 0: the render and loop tasks run on core 1, and Wi-Fi is off while recording.
constexpr BaseType_t TASK_CORE = 0;
constexpr uint32_t READ_TIMEOUT_MS = 100;
constexpr uint32_t HEADER_PATCH_INTERVAL_MS = 2000;
constexpr uint32_t STOP_WAIT_MS = 10000;
constexpr uint32_t MAX_SAMPLES = voicenotes::MAX_RECORDING_SECONDS * voicenotes::SAMPLE_RATE;

void putLe32(uint8_t* p, const uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void putLe16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void buildHeader(uint8_t* h, const uint32_t dataBytes) {
  memcpy(h, "RIFF", 4);
  putLe32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  putLe32(h + 16, 16);  // fmt chunk size
  putLe16(h + 20, 1);   // PCM
  putLe16(h + 22, 1);   // mono
  putLe32(h + 24, voicenotes::SAMPLE_RATE);
  putLe32(h + 28, voicenotes::BYTES_PER_SECOND);
  putLe16(h + 32, sizeof(int16_t));  // block align
  putLe16(h + 34, 16);               // bits per sample
  memcpy(h + 36, "data", 4);
  putLe32(h + 40, dataBytes);
}

}  // namespace

bool VoiceNotesRecorder::start(const char* wavPath) {
  if (task_) return false;
  stopRequested_.store(false);
  taskDone_.store(false);
  failed_.store(false);
  limitReached_.store(false);
  samplesWritten_.store(0);

  if (!Storage.openFileForWrite("VNR", wavPath, file_)) {
    LOG_ERR("VNR", "Cannot create %s", wavPath);
    return false;
  }
  uint8_t header[voicenotes::WAV_HEADER_BYTES];
  buildHeader(header, 0);
  if (file_.write(header, sizeof(header)) != sizeof(header)) {
    LOG_ERR("VNR", "Header write failed");
    file_.close();
    Storage.remove(wavPath);
    return false;
  }

  if (!HalMicrophone::begin()) {
    file_.close();
    Storage.remove(wavPath);
    return false;
  }

  if (xTaskCreatePinnedToCore(&VoiceNotesRecorder::taskEntry, "VoiceRec", TASK_STACK_BYTES, this, TASK_PRIORITY, &task_,
                              TASK_CORE) != pdPASS) {
    LOG_ERR("VNR", "OOM: recorder task (%u byte stack)", static_cast<unsigned>(TASK_STACK_BYTES));
    task_ = nullptr;
    HalMicrophone::end();
    file_.close();
    Storage.remove(wavPath);
    return false;
  }
  LOG_INF("VNR", "Recording %s", wavPath);
  return true;
}

void VoiceNotesRecorder::stop() {
  if (!task_) return;
  stopRequested_.store(true);
  const uint32_t started = millis();
  while (!taskDone_.load() && millis() - started < STOP_WAIT_MS) delay(10);
  if (!taskDone_.load()) LOG_ERR("VNR", "Recorder task did not finish in %u ms", static_cast<unsigned>(STOP_WAIT_MS));
  LOG_DBG("VNR", "Task stack headroom %u bytes", static_cast<unsigned>(uxTaskGetStackHighWaterMark(task_)));
  // The task parks itself in vTaskSuspend once done, holding no locks.
  vTaskDelete(task_);
  task_ = nullptr;

  HalMicrophone::end();
  file_.close();
  LOG_INF("VNR", "Stopped after %u s%s", static_cast<unsigned>(elapsedSeconds()), failed_.load() ? " (failed)" : "");
}

uint32_t VoiceNotesRecorder::elapsedSeconds() const { return samplesWritten_.load() / voicenotes::SAMPLE_RATE; }

void VoiceNotesRecorder::taskEntry(void* self) {
  static_cast<VoiceNotesRecorder*>(self)->run();
  static_cast<VoiceNotesRecorder*>(self)->taskDone_.store(true);
  // The owner deletes this task in stop(); never return from a task function.
  vTaskSuspend(nullptr);
}

bool VoiceNotesRecorder::writeSamples(const size_t count) {
  const size_t bytes = count * sizeof(int16_t);
  if (file_.write(reinterpret_cast<const uint8_t*>(buffer_), bytes) != bytes) return false;
  samplesWritten_.fetch_add(static_cast<uint32_t>(count));
  return true;
}

bool VoiceNotesRecorder::patchHeader() {
  const uint32_t dataBytes = samplesWritten_.load() * sizeof(int16_t);
  uint8_t sizeField[4];
  putLe32(sizeField, 36 + dataBytes);
  if (!file_.seek(4) || file_.write(sizeField, 4) != 4) return false;
  putLe32(sizeField, dataBytes);
  if (!file_.seek(40) || file_.write(sizeField, 4) != 4) return false;
  file_.flush();
  return file_.seek(voicenotes::WAV_HEADER_BYTES + dataBytes);
}

void VoiceNotesRecorder::run() {
  size_t fill = 0;
  uint32_t lastPatch = millis();
  while (!stopRequested_.load()) {
    const int n = HalMicrophone::read(buffer_ + fill, BUFFER_SAMPLES - fill, READ_TIMEOUT_MS);
    if (n < 0) {
      LOG_ERR("VNR", "Microphone read failed");
      failed_.store(true);
      break;
    }
    fill += static_cast<size_t>(n);
    if (samplesWritten_.load() + fill >= MAX_SAMPLES) {
      limitReached_.store(true);
      break;
    }
    if (fill == BUFFER_SAMPLES) {
      if (!writeSamples(fill)) {
        LOG_ERR("VNR", "SD write failed");
        failed_.store(true);
        return;  // leave the header from the last successful patch
      }
      fill = 0;
    }
    if (millis() - lastPatch >= HEADER_PATCH_INTERVAL_MS) {
      if (!patchHeader()) LOG_ERR("VNR", "Header update failed");
      lastPatch = millis();
    }
  }
  if (fill > 0 && !writeSamples(fill)) failed_.store(true);
  if (!patchHeader()) {
    LOG_ERR("VNR", "Final header update failed");
    failed_.store(true);
  }
}
