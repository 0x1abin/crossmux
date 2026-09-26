#include "VoiceNotesPlayer.h"

#if CROSSPOINT_CAP_SOUND_FEEDBACK

#include <HalAudioOutput.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SoundFeedback.h>

#include <atomic>
#include <cstring>

#include "VoiceNotesStore.h"

namespace voicenotes::player {
namespace {

// Reuse the sound-feedback calibration so Low/Medium/High match the system
// setting of the same name: a codec percent per level, plus a digital PCM gain
// for High because the codec is already at 100% for Medium.
constexpr const SoundFeedback::Calibration& CALIBRATION = SoundFeedback::WAVESHARE_397_CALIBRATION;

SoundFeedback::Level levelFor(const Volume volume) {
  switch (volume) {
    case Volume::Low:
      return SoundFeedback::Level::Low;
    case Volume::High:
      return SoundFeedback::Level::High;
    case Volume::Medium:
    default:
      return SoundFeedback::Level::Medium;
  }
}

// Static rather than heap: the playback task reads these through plain function
// pointers, and a single HalFile handle is all a recording needs.
HalFile file;
bool fileOpen = false;
uint32_t dataBytes = 0;
// Byte offset into the PCM data. Written by the audio task during playback and
// read by the main task for the clock; the main task writes it only while the
// audio task is stopped.
std::atomic<uint32_t> dataPos{0};
uint32_t startPos = 0;
// Written by the main task, read by the audio task for the High PCM gain.
std::atomic<Volume> currentVolume{Volume::Medium};

bool seekSource(const size_t position) {
  // AudioManager seeks relative to the start of the stream it was given, which
  // begins at the resume point.
  const uint32_t target = startPos + static_cast<uint32_t>(position);
  if (target > dataBytes || !file.seek(WAV_HEADER_BYTES + target)) return false;
  dataPos.store(target);
  return true;
}

int readSource(uint8_t* dst, const size_t length) {
  const uint32_t pos = dataPos.load();
  if (pos >= dataBytes) return 0;
  size_t want = dataBytes - pos;
  if (want > length) want = length;
  want &= ~static_cast<size_t>(1);  // whole 16-bit samples only
  if (want == 0) return 0;
  const int n = file.read(dst, want);
  if (n < 0) return -1;
  const int whole = n & ~1;
  const SoundFeedback::Level level = levelFor(currentVolume.load());
  if (level == SoundFeedback::Level::High) {
    // memcpy, not a cast: dst carries no 16-bit alignment guarantee.
    for (int offset = 0; offset + 2 <= whole; offset += 2) {
      int16_t sample = 0;
      memcpy(&sample, dst + offset, sizeof(sample));
      sample = SoundFeedback::scalePcmSample(sample, level, CALIBRATION);
      memcpy(dst + offset, &sample, sizeof(sample));
    }
  }
  dataPos.store(pos + static_cast<uint32_t>(whole));
  return whole;
}

void stopOutput() {
  // Blocks until the audio task is idle, so no read callback can race close().
  HalAudioOutput::stop();
}

}  // namespace

bool open(const char* wavPath) {
  close();
  if (!Storage.openFileForRead("VNP", wavPath, file)) {
    LOG_ERR("VNP", "Cannot open %s", wavPath);
    return false;
  }
  uint8_t header[WAV_HEADER_BYTES];
  const size_t size = file.fileSize();
  if (size <= WAV_HEADER_BYTES || file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    LOG_ERR("VNP", "Not a recording: %s", wavPath);
    file.close();
    return false;
  }
  // The header sizes can lag after a power loss mid-recording (the recorder
  // patches them every few seconds), so trust the file size like the list does.
  dataBytes = static_cast<uint32_t>(size - WAV_HEADER_BYTES) & ~1u;
  dataPos.store(0);
  startPos = 0;
  fileOpen = true;
  LOG_INF("VNP", "Opened %s (%u s)", wavPath, static_cast<unsigned>(durationSeconds()));
  return true;
}

bool play(const uint32_t fromSecond) {
  if (!fileOpen) return false;
  stopOutput();
  uint32_t from = fromSecond * BYTES_PER_SECOND;
  if (from >= dataBytes) from = 0;  // at the end: replay from the start
  startPos = from;
  const uint8_t codecVolume = SoundFeedback::volumeForLevel(levelFor(currentVolume.load()), CALIBRATION);
  if (!HalAudioOutput::playPcm(readSource, seekSource, SAMPLE_RATE, 1, codecVolume)) {
    LOG_ERR("VNP", "Speaker unavailable");
    return false;
  }
  return true;
}

void pause() {
  if (fileOpen) stopOutput();
}

void close() {
  if (!fileOpen) return;
  HalAudioOutput::shutdown();
  file.close();
  fileOpen = false;
  dataBytes = 0;
  dataPos.store(0);
  startPos = 0;
}

bool active() { return fileOpen; }

bool playing() { return fileOpen && HalAudioOutput::isPlaying(); }

uint32_t positionSeconds() { return dataPos.load() / BYTES_PER_SECOND; }

uint32_t durationSeconds() { return dataBytes / BYTES_PER_SECOND; }

Volume volume() { return currentVolume.load(); }

void setVolume(const Volume volume) {
  currentVolume.store(volume);
  // setVolume() is a no-op while the codec is powered down; play() applies it.
  HalAudioOutput::setVolume(SoundFeedback::volumeForLevel(levelFor(volume), CALIBRATION));
}

}  // namespace voicenotes::player

#else

namespace voicenotes::player {
bool open(const char*) { return false; }
bool play(uint32_t) { return false; }
void pause() {}
void close() {}
bool active() { return false; }
bool playing() { return false; }
uint32_t positionSeconds() { return 0; }
uint32_t durationSeconds() { return 0; }
Volume volume() { return Volume::Medium; }
void setVolume(Volume) {}
}  // namespace voicenotes::player

#endif
