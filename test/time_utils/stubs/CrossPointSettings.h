#pragma once

#include <cstdint>

class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }

  uint8_t clockUtcOffsetQ = 48;
  uint8_t clockTimezone = 255;
  enum CLOCK_DST_MODE { CLOCK_DST_AUTO, CLOCK_DST_ON, CLOCK_DST_OFF, CLOCK_DST_MODE_COUNT };
  uint8_t clockDst = CLOCK_DST_AUTO;
};

#define SETTINGS CrossPointSettings::getInstance()
