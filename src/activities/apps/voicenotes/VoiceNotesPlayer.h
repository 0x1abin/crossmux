#pragma once

#include <cstdint>

// Streams a recording's PCM from SD to the speaker. HalAudioOutput pulls data
// through plain function pointers on its own task, so the player is a single
// static instance: one recording plays at a time. All calls are main task only.
namespace voicenotes::player {

// Opens the WAV and validates its header. Does not start the speaker.
bool open(const char* wavPath);
// Starts (or restarts) output at `fromSecond`, clamped to the recording.
bool play(uint32_t fromSecond);
// Stops output but keeps the file and position for play().
void pause();
// Stops output, powers the codec down, and closes the file. Idempotent.
void close();

// True while a file is open (playing or paused); main.cpp holds off button
// sound feedback so a cue never replaces the recording on the shared codec.
bool active();
// True while the speaker task is still streaming; false once playback ends.
bool playing();
uint32_t positionSeconds();
uint32_t durationSeconds();

}  // namespace voicenotes::player
