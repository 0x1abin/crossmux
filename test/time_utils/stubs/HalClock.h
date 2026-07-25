#pragma once

#include <ctime>

class HalClock {
 public:
  time_t now = 0;

  bool hasValidTime() const { return now != 0; }
  time_t nowUtc() const { return now; }
};

inline HalClock halClock;
