#pragma once

#include <cstdint>

// Shared time helpers used by Standby and its faces.
namespace standby_time {

bool isSynced();

// Return false when no trustworthy wall clock is available.
bool getNowHHMM(unsigned& hh, unsigned& mm);

uint32_t getMinuteTick();

}  // namespace standby_time
