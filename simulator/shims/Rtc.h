#pragma once

#include <cstdint>

class Rtc {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

  bool begin() { return false; }
  bool present() const { return false; }
  bool now(DateTime&) { return false; }
  bool set(const DateTime&) { return false; }
  bool adjust(int32_t, DateTime* = nullptr) { return false; }
};
