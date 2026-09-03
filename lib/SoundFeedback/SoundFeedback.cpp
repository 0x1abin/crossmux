#include "SoundFeedback.h"

#include <cstring>

namespace SoundFeedback {
namespace {

uint16_t readLe16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8; }

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]) << 16 |
         static_cast<uint32_t>(data[3]) << 24;
}

}  // namespace

bool parsePcmWav(const uint8_t* wav, const size_t length, WavPcmView& view) {
  view = {};
  if (!wav || length < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool haveFormat = false;
  size_t position = 12;
  while (position <= length && length - position >= 8) {
    const uint8_t* chunk = wav + position;
    const uint32_t chunkLength = readLe32(chunk + 4);
    const size_t dataPosition = position + 8;
    if (chunkLength > length - dataPosition) return false;

    if (memcmp(chunk, "fmt ", 4) == 0) {
      if (chunkLength < 16 || readLe16(wav + dataPosition) != 1 || readLe16(wav + dataPosition + 14) != 16) {
        return false;
      }
      view.channels = static_cast<uint8_t>(readLe16(wav + dataPosition + 2));
      view.sampleRate = readLe32(wav + dataPosition + 4);
      haveFormat = true;
    } else if (memcmp(chunk, "data", 4) == 0) {
      view.data = wav + dataPosition;
      view.length = chunkLength;
    }

    const size_t paddedLength = static_cast<size_t>(chunkLength) + (chunkLength & 1u);
    if (paddedLength > length - dataPosition) break;
    position = dataPosition + paddedLength;
  }

  return haveFormat && view.data && view.length > 0 && (view.length & 1u) == 0 &&
         (view.channels == 1 || view.channels == 2) && view.sampleRate >= 8000 && view.sampleRate <= 48000;
}

}  // namespace SoundFeedback

#if CROSSPOINT_CAP_SOUND_FEEDBACK

#include <HalAudioOutput.h>
#include <Logging.h>

#include <atomic>

namespace SoundFeedback {
namespace {

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
constexpr const Calibration& ACTIVE_CALIBRATION = WAVESHARE_397_CALIBRATION;
#else
#error "CROSSPOINT_CAP_SOUND_FEEDBACK requires a board calibration"
#endif

constexpr uint32_t FEEDBACK_SAMPLE_RATE = 16000;
constexpr uint8_t FEEDBACK_CHANNELS = 1;

extern const uint8_t selectWavStart[] asm("_binary_assets_sounds_select_wav_start");
extern const uint8_t selectWavEnd[] asm("_binary_assets_sounds_select_wav_end");
extern const uint8_t tapWavStart[] asm("_binary_assets_sounds_tap_wav_start");
extern const uint8_t tapWavEnd[] asm("_binary_assets_sounds_tap_wav_end");

WavPcmView selectPcm;
WavPcmView tapPcm;
const WavPcmView* activePcm = nullptr;
size_t sourceOffset = 0;
Level activeLevel = Level::Off;
Level currentLevel = Level::Off;
std::atomic<uint8_t> requested{0};
uint8_t activeRequest = 0;
bool resourcesChecked = false;
bool resourcesValid = false;

constexpr uint8_t encodeRequest(const Cue cue, const Level level) {
  return static_cast<uint8_t>(cue) | static_cast<uint8_t>(static_cast<uint8_t>(level) << 2);
}

constexpr Cue decodeCue(const uint8_t value) { return static_cast<Cue>(value & 0x03u); }
constexpr Level decodeLevel(const uint8_t value) { return levelFromSetting((value >> 2) & 0x03u); }

bool ensureResources() {
  if (resourcesChecked) return resourcesValid;
  resourcesChecked = true;
  resourcesValid = parsePcmWav(selectWavStart, static_cast<size_t>(selectWavEnd - selectWavStart), selectPcm) &&
                   parsePcmWav(tapWavStart, static_cast<size_t>(tapWavEnd - tapWavStart), tapPcm) &&
                   selectPcm.sampleRate == FEEDBACK_SAMPLE_RATE && tapPcm.sampleRate == FEEDBACK_SAMPLE_RATE &&
                   selectPcm.channels == FEEDBACK_CHANNELS && tapPcm.channels == FEEDBACK_CHANNELS;
  if (!resourcesValid) LOG_ERR("SND", "Invalid embedded feedback WAV");
  return resourcesValid;
}

bool seekSource(const size_t position) {
  const uint8_t snapshot = requested.load(std::memory_order_acquire);
  const Cue cue = decodeCue(snapshot);
  const WavPcmView* pcm = cue == Cue::Move ? &selectPcm : cue == Cue::Activate ? &tapPcm : nullptr;
  if (!pcm || position > pcm->length) return false;
  activePcm = pcm;
  activeLevel = decodeLevel(snapshot);
  activeRequest = snapshot;
  sourceOffset = position;
  return true;
}

int readSource(uint8_t* dst, const size_t requestedLength) {
  // A second key can arrive while AudioManager is consuming RestartPending.
  // Adopt the newest atomic cue/level snapshot at this same PCM boundary.
  const uint8_t snapshot = requested.load(std::memory_order_acquire);
  if (snapshot != activeRequest && !seekSource(0)) return -1;
  if (!activePcm || sourceOffset >= activePcm->length) return 0;
  size_t length = activePcm->length - sourceOffset;
  if (length > requestedLength) length = requestedLength;
  memcpy(dst, activePcm->data + sourceOffset, length);
  if (activeLevel == Level::High) {
    for (size_t offset = 0; offset + sizeof(int16_t) <= length; offset += sizeof(int16_t)) {
      int16_t sample = 0;
      memcpy(&sample, dst + offset, sizeof(sample));
      sample = scalePcmSample(sample, activeLevel, ACTIVE_CALIBRATION);
      memcpy(dst + offset, &sample, sizeof(sample));
    }
  }
  sourceOffset += length;
  return static_cast<int>(length);
}

}  // namespace

void update(const uint8_t levelSetting, const uint8_t physicalPressedMask) {
  const Level level = levelFromSetting(levelSetting);
  if (level != currentLevel) {
    currentLevel = level;
    if (level == Level::Off) {
      shutdown();
      return;
    }
    HalAudioOutput::setVolume(volumeForLevel(level, ACTIVE_CALIBRATION));
  }

  const Cue cue = cueForPhysicalMask(physicalPressedMask);
  if (level == Level::Off || cue == Cue::None || !ensureResources()) return;

  requested.store(encodeRequest(cue, level), std::memory_order_release);
  const uint8_t volume = volumeForLevel(level, ACTIVE_CALIBRATION);
  if (HalAudioOutput::restart(volume)) return;
  HalAudioOutput::playPcm(readSource, seekSource, FEEDBACK_SAMPLE_RATE, FEEDBACK_CHANNELS, volume);
}

void shutdown() {
  HalAudioOutput::shutdown();
  requested.store(0, std::memory_order_relaxed);
  activePcm = nullptr;
  activeLevel = Level::Off;
  activeRequest = 0;
  sourceOffset = 0;
}

}  // namespace SoundFeedback

#endif
