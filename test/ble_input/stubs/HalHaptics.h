#pragma once
#include <cstdint>

// Record the application policy; motor PWM/timing belongs to SDK device tests.
struct HalHaptics {
  static inline int pulses = 0;
  static inline int longPresses = 0;
  static inline uint8_t lastIntensity = 0;
  static void reset() { pulses = longPresses = lastIntensity = 0; }
  static void feedback(bool enabled, bool pressed, uint8_t intensity) {
    if (enabled && pressed) {
      ++pulses;
      lastIntensity = intensity;
    }
  }
  static void longPress(bool enabled, uint8_t intensity) {
    if (enabled) {
      ++longPresses;
      lastIntensity = intensity;
    }
  }
};
