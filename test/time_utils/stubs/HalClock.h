#pragma once

#include <cstdlib>
#include <ctime>

class HalClock {
 public:
  time_t now = 0;

  bool hasValidTime() const { return now != 0; }
  time_t nowUtc() const { return now; }
  void setTimezone(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
  }
};

inline HalClock halClock;
