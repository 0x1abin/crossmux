#pragma once

#include <cstdint>

#include "WeReadBackend.h"

struct WeReadProgressContext {
  float localFraction = 0.0f;
  uint32_t localTocIndex = 0;
  uint32_t localOffset = 0;
  uint16_t localSpineIndex = 0;
  uint16_t localPageNumber = 0;
  uint16_t localPageCount = 0;
  WeReadBackend::Client::LocalOffsetBasis localOffsetBasis = WeReadBackend::Client::LocalOffsetBasis::None;
};

static_assert(sizeof(WeReadProgressContext) == 20);
