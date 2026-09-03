#include <gtest/gtest.h>

#include <array>

#include "SoundFeedback.h"

using SoundFeedback::Cue;
using SoundFeedback::Level;

TEST(SoundFeedback, ChoosesOneCuePerPhysicalInputFrame) {
  EXPECT_EQ(SoundFeedback::cueForPhysicalMask(0), Cue::None);
  EXPECT_EQ(SoundFeedback::cueForPhysicalMask(1u << SoundFeedback::BUTTON_LEFT), Cue::Move);
  EXPECT_EQ(SoundFeedback::cueForPhysicalMask(1u << SoundFeedback::BUTTON_UP), Cue::Move);
  EXPECT_EQ(SoundFeedback::cueForPhysicalMask(1u << SoundFeedback::BUTTON_CONFIRM), Cue::Activate);
  EXPECT_EQ(SoundFeedback::cueForPhysicalMask((1u << SoundFeedback::BUTTON_LEFT) | (1u << SoundFeedback::BUTTON_BACK)),
            Cue::Activate);
}

TEST(SoundFeedback, MapsWaveshareLevelsAndDefaultsInvalidValuesToMedium) {
  const auto& calibration = SoundFeedback::WAVESHARE_397_CALIBRATION;
  EXPECT_EQ(SoundFeedback::volumeForLevel(Level::Off, calibration), 0);
  EXPECT_EQ(SoundFeedback::volumeForLevel(Level::Low, calibration), 85);
  EXPECT_EQ(SoundFeedback::volumeForLevel(Level::Medium, calibration), 100);
  EXPECT_EQ(SoundFeedback::volumeForLevel(Level::High, calibration), 100);
  EXPECT_EQ(SoundFeedback::levelFromSetting(0), Level::Off);
  EXPECT_EQ(SoundFeedback::levelFromSetting(1), Level::Low);
  EXPECT_EQ(SoundFeedback::levelFromSetting(2), Level::Medium);
  EXPECT_EQ(SoundFeedback::levelFromSetting(3), Level::High);
  EXPECT_EQ(SoundFeedback::levelFromSetting(4), Level::Medium);
}

TEST(SoundFeedback, AmplifiesOnlyHighWithSaturation) {
  const auto& calibration = SoundFeedback::WAVESHARE_397_CALIBRATION;
  EXPECT_EQ(SoundFeedback::scalePcmSample(12345, Level::Off, calibration), 12345);
  EXPECT_EQ(SoundFeedback::scalePcmSample(12345, Level::Low, calibration), 12345);
  EXPECT_EQ(SoundFeedback::scalePcmSample(12345, Level::Medium, calibration), 12345);
  EXPECT_EQ(SoundFeedback::scalePcmSample(1000, Level::High, calibration), 1750);
  EXPECT_EQ(SoundFeedback::scalePcmSample(-1000, Level::High, calibration), -1750);
  EXPECT_EQ(SoundFeedback::scalePcmSample(30000, Level::High, calibration), 32767);
  EXPECT_EQ(SoundFeedback::scalePcmSample(-30000, Level::High, calibration), -32768);
}

TEST(SoundFeedback, ParsesPcmDataAfterNonstandardChunks) {
  constexpr std::array<uint8_t, 60> wav = {
      'R', 'I', 'F', 'F', 52,   0,    0, 0, 'W',  'A',  'V', 'E', 'f', 'm', 't', ' ', 16,   0,    0,    0,
      1,   0,   1,   0,   0x80, 0x3E, 0, 0, 0x00, 0x7D, 0,   0,   2,   0,   16,  0,   'J',  'U',  'N',  'K',
      3,   0,   0,   0,   1,    2,    3, 0, 'd',  'a',  't', 'a', 4,   0,   0,   0,   0x34, 0x12, 0xCC, 0xED};
  SoundFeedback::WavPcmView view;
  ASSERT_TRUE(SoundFeedback::parsePcmWav(wav.data(), wav.size(), view));
  EXPECT_EQ(view.data, wav.data() + 56);
  EXPECT_EQ(view.length, 4u);
  EXPECT_EQ(view.sampleRate, 16000u);
  EXPECT_EQ(view.channels, 1u);
}

TEST(SoundFeedback, RejectsTruncatedPcmChunk) {
  constexpr std::array<uint8_t, 44> wav = {'R', 'I', 'F', 'F', 36, 0, 0,   0,   'W', 'A',  'V',  'E', 'f', 'm',  't',
                                           ' ', 16,  0,   0,   0,  1, 0,   1,   0,   0x80, 0x3E, 0,   0,   0x00, 0x7D,
                                           0,   0,   2,   0,   16, 0, 'd', 'a', 't', 'a',  8,    0,   0,   0};
  SoundFeedback::WavPcmView view;
  EXPECT_FALSE(SoundFeedback::parsePcmWav(wav.data(), wav.size(), view));
}
