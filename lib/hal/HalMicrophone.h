#pragma once

#include <cstddef>
#include <cstdint>

// Analog microphone behind the board's ES8311 codec ADC (Waveshare ePaper 3.97).
// PDM-mic boards use freeink::Microphone instead; this HAL exists because the
// SDK's MicConfig has no I2S-codec input yet. Capture and HalAudioOutput share
// the codec and I2S port, so begin() powers playback down and main.cpp skips
// sound feedback while active().
namespace HalMicrophone {

inline constexpr uint32_t SAMPLE_RATE = 16000;

// Powers the codec rail, configures the ES8311 ADC, and starts I2S RX.
bool begin();
// Reads up to maxSamples 16-bit mono samples. Returns the count read (0 on
// timeout) or <0 when capture is not running or the driver fails.
int read(int16_t* dst, size_t maxSamples, uint32_t timeoutMs);
// Stops I2S RX, powers the ADC down, and drops the codec rail.
void end();
bool active();

}  // namespace HalMicrophone
