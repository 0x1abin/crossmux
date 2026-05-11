#include "GameUi.h"

#include <cstdio>

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen) {
  const uint32_t totalSec = ms / 1000;
  const uint32_t mm = (totalSec / 60) % 100;
  const uint32_t ss = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}
