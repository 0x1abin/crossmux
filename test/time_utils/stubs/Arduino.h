#pragma once

#include <cstdint>

inline uint32_t arduinoTestMillis = 0;

inline unsigned long millis() { return arduinoTestMillis; }
