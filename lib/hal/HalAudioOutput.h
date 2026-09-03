#pragma once

#include <cstddef>
#include <cstdint>

namespace HalAudioOutput {

using ReadCallback = int (*)(uint8_t* dst, size_t length);
using SeekCallback = bool (*)(size_t position);

bool playPcm(ReadCallback read, SeekCallback seek, uint32_t sampleRate, uint8_t channels, uint8_t volume);
bool restart(uint8_t volume);
void setVolume(uint8_t volume);
void shutdown();

}  // namespace HalAudioOutput
