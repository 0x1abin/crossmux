#pragma once

#include <cstdint>

struct WeReadProgressContext {
  float localFraction = 0.0f;
  uint32_t localTocIndex = 0;
  uint16_t localSpineIndex = 0;
  uint16_t localPageNumber = 0;
  uint16_t localPageCount = 0;
  bool hasLocalTocIndex = false;
};

static_assert(sizeof(WeReadProgressContext) == 16);
