#include "HalAudioOutput.h"

#if CROSSPOINT_CAP_SOUND_FEEDBACK

#include <AudioManager.h>
#include <BoardConfig.h>
#include <Logging.h>

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
#include "Waveshare397Power.h"
#endif

namespace HalAudioOutput {
namespace {

constexpr unsigned long AUDIO_RAIL_SETTLE_MS = 10;

AudioManager audio;
bool initialized = false;

bool setBoardPower(const bool enabled) {
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  return Waveshare397Power::setAudioPower(enabled);
#else
  (void)enabled;
  return true;
#endif
}

bool begin() {
  if (initialized) return true;
  if (!setBoardPower(true)) {
    LOG_ERR("SND", "Failed to enable board audio power");
    return false;
  }
#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
  delay(AUDIO_RAIL_SETTLE_MS);
#endif
  if (!audio.begin()) {
    LOG_ERR("SND", "Failed to initialize audio output");
    setBoardPower(false);
    return false;
  }
  initialized = true;
  return true;
}

}  // namespace

bool playPcm(const ReadCallback read, const SeekCallback seek, const uint32_t sampleRate, const uint8_t channels,
             const uint8_t volume) {
  if (!read || !begin()) return false;
  audio.setVolume(volume);
  AudioManager::PcmSource source = {read, seek};
  if (audio.playPcm(source, sampleRate, channels)) return true;
  LOG_ERR("SND", "Failed to play feedback PCM");
  shutdown();
  return false;
}

bool restart(const uint8_t volume) {
  if (!initialized) return false;
  audio.setVolume(volume);
  return audio.requestRestart();
}

void setVolume(const uint8_t volume) {
  if (initialized) audio.setVolume(volume);
}

void shutdown() {
  if (!initialized) return;
  audio.powerDown();
  if (!setBoardPower(false)) LOG_ERR("SND", "Failed to disable board audio power");
  initialized = false;
}

}  // namespace HalAudioOutput

#endif
