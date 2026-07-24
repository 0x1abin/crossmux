#pragma once

#include <cstdint>

class CrossPointSettings {
 public:
  static CrossPointSettings& getInstance() {
    static CrossPointSettings instance;
    return instance;
  }

  uint8_t clockUtcOffsetQ = 48;
};

#define SETTINGS CrossPointSettings::getInstance()
