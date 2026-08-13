#pragma once

#include <cstdint>

// Shared time helpers used by Standby and its faces.
namespace standby_time {

bool isSynced();

// Use the trustworthy wall clock when available. Before sync, tick forward
// from a plausible fallback time anchored at fallbackStartMs.
void getNowHHMM(uint32_t fallbackStartMs, unsigned& hh, unsigned& mm);

uint32_t getMinuteTick(uint32_t fallbackStartMs);

}  // namespace standby_time
