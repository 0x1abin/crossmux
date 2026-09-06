#pragma once

#include <cstdint>

struct EspHostStub {
  uint32_t freeHeap = UINT32_MAX;
  uint32_t maxAlloc = UINT32_MAX;
  mutable int callsUntilLow = -1;
  uint32_t getFreeHeap() const {
    if (callsUntilLow == 0) return 0;
    if (callsUntilLow > 0) --callsUntilLow;
    return freeHeap;
  }
  uint32_t getMinFreeHeap() const { return freeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
};

inline EspHostStub ESP;
