#pragma once

#include <cstdint>

// Shared time helpers used by Standby and its faces. The session flag means
// the system clock was accepted from RTC, NTP, or the host.
namespace standby_time {

// True if the system clock is trusted during this boot session.
bool isSynced();

// Setter called when Standby accepts a trustworthy system clock.
void setSynced(bool v);

// Return the current wall-clock HH and MM. When isSynced() is false the result
// is a fallback computed by ticking forward from a plausible start time using
// the caller-supplied millis() anchor (`fallbackStartMs`), so the display looks
// alive instead of stuck at 00:00.
void getNowHHMM(uint32_t fallbackStartMs, unsigned& hh, unsigned& mm);

// Minute-boundary tick used by faces to decide "did the minute change since
// last render?". Same fallback semantics as getNowHHMM().
uint32_t getMinuteTick(uint32_t fallbackStartMs);

}  // namespace standby_time
