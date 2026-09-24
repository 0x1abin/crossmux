#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Recordings live in /recordings as RECnnnn.wav (16 kHz mono PCM16) with an
// optional RECnnnn.txt transcript beside each one.
namespace voicenotes {

inline constexpr char DIR[] = "/recordings";
inline constexpr uint32_t SAMPLE_RATE = 16000;
inline constexpr uint32_t WAV_HEADER_BYTES = 44;
inline constexpr uint32_t BYTES_PER_SECOND = SAMPLE_RATE * sizeof(int16_t);
// Whisper accepts 25 MB; 12 min of 16 kHz PCM16 is ~23 MB.
inline constexpr uint32_t MAX_RECORDING_SECONDS = 12 * 60;
// Bounds the list's heap use; older recordings stay on the card.
inline constexpr size_t MAX_LISTED = 200;
inline constexpr size_t NAME_LEN = 8;  // "REC0001" + NUL
inline constexpr size_t PATH_LEN = 32;

struct Recording {
  char name[NAME_LEN];
  uint32_t bytes;
  bool hasTranscript;
};

// Fills `out` newest first. Returns false when the folder cannot be read.
bool list(std::vector<Recording>& out);
// Creates the folder if needed and writes the next free "/recordings/RECnnnn.wav".
bool nextWavPath(char* out, size_t outLen);
void wavPath(const char* name, char* out, size_t outLen);
void txtPath(const char* name, char* out, size_t outLen);
// Deletes the recording and its transcript.
bool remove(const char* name);
uint32_t durationSeconds(uint32_t wavBytes);

}  // namespace voicenotes
