#include "HalMicrophone.h"

#if CROSSPOINT_CAP_VOICE_RECORDER

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Wire.h>
#include <driver/i2s_std.h>

#include <atomic>

#include "HalAudioOutput.h"

#if FREEINK_DEVICE_WAVESHARE_EPAPER_397
#include "Waveshare397Power.h"
#else
#error "CROSSPOINT_CAP_VOICE_RECORDER requires a board microphone mapping"
#endif

namespace HalMicrophone {
namespace {

// ES8311 ASDOUT -> ESP32 I2S DIN. BoardConfig's AudioConfig describes only the
// playback path, so the capture pin lives here until the SDK grows an
// I2S-codec MicConfig. Source: Waveshare ESP32-S3-ePaper-3.97 examples
// (01_Audio_Test.ino I2S_DIN_PIN, es8311_bsp.h I2S_DATA_PIN).
constexpr gpio_num_t MIC_DIN = GPIO_NUM_21;

constexpr uint32_t CODEC_I2C_HZ = 100000;  // same bus speed AudioManager uses
constexpr unsigned long AUDIO_RAIL_SETTLE_MS = 10;
constexpr unsigned long CODEC_RESET_MS = 5;
// 8 x 512 stereo frames = 16 KB of DMA RAM, i.e. 256 ms of slack at 16 kHz so
// an SD write stall in the recorder task does not drop samples.
constexpr uint32_t DMA_DESC_NUM = 8;
constexpr uint32_t DMA_FRAME_NUM = 512;
constexpr size_t READ_FRAMES = 256;

struct RegVal {
  uint8_t reg;
  uint8_t val;
};

// ES8311 capture bring-up: slave mode, MCLK = 256*fs from the ESP32, 16 kHz,
// 16-bit I2S, analog mic on MIC1P/N, 24 dB PGA. Values follow Espressif's
// es8311 driver (esp-bsp / esp-codec-dev, Apache-2.0); the clock block matches
// AudioManager's ES8311_MCLK_16K_INIT for this board.
constexpr RegVal ES8311_CAPTURE_INIT[] = {
    {0x01, 0x3F},                // CLK_MANAGER: MCLK from pin, all clocks on
    {0x02, 0x00},                // CLK_MANAGER: pre-divide 1, multiply 1
    {0x03, 0x10},                // CLK_MANAGER: single speed, ADC OSR
    {0x04, 0x20},                // CLK_MANAGER: DAC OSR
    {0x05, 0x00},                // CLK_MANAGER: ADC/DAC clock divide 1
    {0x06, 0x03},                // CLK_MANAGER: BCLK divider (master mode only)
    {0x07, 0x00},                // CLK_MANAGER: LRCK = MCLK / 256
    {0x08, 0xFF}, {0x00, 0x80},  // RESET: state machine on, slave mode
    {0x09, 0x0C},                // SDP in: I2S, 16-bit
    {0x0A, 0x0C},                // SDP out: I2S, 16-bit
    {0x0D, 0x01},                // SYSTEM: power up analog circuitry
    {0x0E, 0x02},                // SYSTEM: enable PGA and ADC modulator
    {0x12, 0x00},                // SYSTEM: DAC powered (reference for the ADC path)
    {0x13, 0x10},                // SYSTEM: HP drive
    {0x1B, 0x0A},                // ADC: high-pass filter stage 1
    {0x1C, 0x6A},                // ADC: bypass EQ, cancel DC offset
    {0x14, 0x1A},                // SYSTEM: analog mic input, PGA enabled
    {0x16, 0x04},                // ADC: mic PGA gain 24 dB (6 dB/step)
    {0x15, 0x40},                // ADC: ramp rate
    {0x17, 0xBF},                // ADC: digital volume 0 dB
    {0x31, 0x60},                // DAC: muted, so nothing leaks to the speaker
    {0x32, 0x00},                // DAC: volume off
    {0x37, 0x08},                // DAC: bypass EQ
};

constexpr RegVal ES8311_CAPTURE_OFF[] = {
    {0x17, 0x00},  // ADC digital volume off
    {0x0E, 0xFF},  // PGA and ADC modulator off
    {0x14, 0x00},  // mic input off
    {0x0D, 0xFA},  // analog circuitry down
};

i2s_chan_handle_t rxChan = nullptr;
std::atomic<bool> running{false};
// Only the recorder task calls read(); one static block avoids a per-call
// 1 KB stack array on that task.
int16_t stereoFrames[READ_FRAMES * 2];

bool codecWrite(const uint8_t addr, const uint8_t reg, const uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool codecWriteAll(const RegVal* seq, const size_t count) {
  const uint8_t addr = BoardConfig::ACTIVE.audio.codecAddr;
  for (size_t i = 0; i < count; ++i) {
    if (!codecWrite(addr, seq[i].reg, seq[i].val)) {
      LOG_ERR("MIC", "ES8311 write 0x%02X failed", seq[i].reg);
      return false;
    }
  }
  return true;
}

bool codecInit() {
  const auto& cfg = BoardConfig::ACTIVE.audio;
  Wire.begin(cfg.codecSda, cfg.codecScl, CODEC_I2C_HZ);
  // Reset pulse clears whatever the playback init left in the ADC/DAC blocks.
  if (!codecWrite(cfg.codecAddr, 0x00, 0x1F)) return false;
  delay(CODEC_RESET_MS);
  if (!codecWrite(cfg.codecAddr, 0x00, 0x00)) return false;
  return codecWriteAll(ES8311_CAPTURE_INIT, sizeof(ES8311_CAPTURE_INIT) / sizeof(ES8311_CAPTURE_INIT[0]));
}

bool startI2s() {
  const auto& cfg = BoardConfig::ACTIVE.audio;
  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = DMA_DESC_NUM;
  chanCfg.dma_frame_num = DMA_FRAME_NUM;
  if (i2s_new_channel(&chanCfg, nullptr, &rxChan) != ESP_OK) {
    rxChan = nullptr;
    return false;
  }

  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = static_cast<gpio_num_t>(cfg.mclk);
  std.gpio_cfg.bclk = static_cast<gpio_num_t>(cfg.bclk);
  std.gpio_cfg.ws = static_cast<gpio_num_t>(cfg.lrclk);
  std.gpio_cfg.dout = I2S_GPIO_UNUSED;
  std.gpio_cfg.din = MIC_DIN;

  if (i2s_channel_init_std_mode(rxChan, &std) != ESP_OK || i2s_channel_enable(rxChan) != ESP_OK) {
    i2s_del_channel(rxChan);
    rxChan = nullptr;
    return false;
  }
  return true;
}

void stopI2s() {
  if (!rxChan) return;
  i2s_channel_disable(rxChan);
  i2s_del_channel(rxChan);
  rxChan = nullptr;
}

}  // namespace

bool begin() {
  if (running.load()) return true;

  // Playback owns I2S_NUM_0 and the codec registers while a cue is live.
  HalAudioOutput::shutdown();

  if (!Waveshare397Power::setAudioPower(true)) {
    LOG_ERR("MIC", "Failed to enable audio rail");
    return false;
  }
  delay(AUDIO_RAIL_SETTLE_MS);

  // MCLK must run before the codec's clock manager is programmed.
  if (!startI2s()) {
    LOG_ERR("MIC", "I2S RX init failed");
    Waveshare397Power::setAudioPower(false);
    return false;
  }
  if (!codecInit()) {
    LOG_ERR("MIC", "ES8311 capture init failed");
    stopI2s();
    Waveshare397Power::setAudioPower(false);
    return false;
  }

  running.store(true);
  LOG_INF("MIC", "Capture started at %u Hz", static_cast<unsigned>(SAMPLE_RATE));
  return true;
}

int read(int16_t* dst, const size_t maxSamples, const uint32_t timeoutMs) {
  if (!running.load() || !rxChan || !dst) return -1;
  size_t frames = maxSamples < READ_FRAMES ? maxSamples : READ_FRAMES;
  size_t bytesRead = 0;
  const esp_err_t err =
      i2s_channel_read(rxChan, stereoFrames, frames * 2 * sizeof(int16_t), &bytesRead, pdMS_TO_TICKS(timeoutMs));
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return -1;
  frames = bytesRead / (2 * sizeof(int16_t));
  // The ES8311 ADC drives the left slot.
  for (size_t i = 0; i < frames; ++i) dst[i] = stereoFrames[i * 2];
  return static_cast<int>(frames);
}

void end() {
  if (!running.exchange(false)) return;
  codecWriteAll(ES8311_CAPTURE_OFF, sizeof(ES8311_CAPTURE_OFF) / sizeof(ES8311_CAPTURE_OFF[0]));
  stopI2s();
  if (!Waveshare397Power::setAudioPower(false)) LOG_ERR("MIC", "Failed to disable audio rail");
  LOG_INF("MIC", "Capture stopped");
}

bool active() { return running.load(); }

}  // namespace HalMicrophone

#endif
