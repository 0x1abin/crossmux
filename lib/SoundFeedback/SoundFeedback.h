#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace SoundFeedback {

enum class Cue : uint8_t { None, Move, Activate };
enum class Level : uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };

inline constexpr uint8_t BUTTON_BACK = 0;
inline constexpr uint8_t BUTTON_CONFIRM = 1;
inline constexpr uint8_t BUTTON_LEFT = 2;
inline constexpr uint8_t BUTTON_RIGHT = 3;
inline constexpr uint8_t BUTTON_UP = 4;
inline constexpr uint8_t BUTTON_DOWN = 5;
inline constexpr uint8_t BUTTON_POWER = 6;

struct Calibration {
  uint8_t codecVolume[4];
  int16_t highGainNumerator;
  int16_t highGainDenominator;
};

struct WavPcmView {
  const uint8_t* data = nullptr;
  size_t length = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
};

inline constexpr Calibration WAVESHARE_397_CALIBRATION = {{0, 85, 100, 100}, 7, 4};

constexpr Level levelFromSetting(const uint8_t value) {
  switch (value) {
    case static_cast<uint8_t>(Level::Off):
      return Level::Off;
    case static_cast<uint8_t>(Level::Low):
      return Level::Low;
    case static_cast<uint8_t>(Level::Medium):
      return Level::Medium;
    case static_cast<uint8_t>(Level::High):
      return Level::High;
    default:
      return Level::Medium;
  }
}

constexpr Cue cueForPhysicalMask(const uint8_t mask) {
  constexpr uint8_t ACTION_MASK = (1u << BUTTON_BACK) | (1u << BUTTON_CONFIRM) | (1u << BUTTON_POWER);
  constexpr uint8_t DIRECTION_MASK =
      (1u << BUTTON_LEFT) | (1u << BUTTON_RIGHT) | (1u << BUTTON_UP) | (1u << BUTTON_DOWN);
  return (mask & ACTION_MASK) != 0 ? Cue::Activate : (mask & DIRECTION_MASK) != 0 ? Cue::Move : Cue::None;
}

constexpr uint8_t volumeForLevel(const Level level, const Calibration& calibration) {
  return calibration.codecVolume[static_cast<uint8_t>(level)];
}

constexpr int16_t scalePcmSample(const int16_t sample, const Level level, const Calibration& calibration) {
  if (level != Level::High) return sample;
  const int32_t scaled = static_cast<int32_t>(sample) * calibration.highGainNumerator / calibration.highGainDenominator;
  if (scaled > std::numeric_limits<int16_t>::max()) return std::numeric_limits<int16_t>::max();
  if (scaled < std::numeric_limits<int16_t>::min()) return std::numeric_limits<int16_t>::min();
  return static_cast<int16_t>(scaled);
}

bool parsePcmWav(const uint8_t* wav, size_t length, WavPcmView& view);
void update(uint8_t levelSetting, uint8_t physicalPressedMask);
void shutdown();

}  // namespace SoundFeedback
