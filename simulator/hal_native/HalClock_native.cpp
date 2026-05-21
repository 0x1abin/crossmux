// HAL clock stub for the simulator. The real HalClock drives a DS3231 RTC over
// I2C (an X3 feature); the simulated X4 has no RTC, so the clock reports
// unavailable and callers fall back to NTP-synced / fallback time exactly as on
// X4 hardware. Mirrors the minimal-stub style of the other hal_native files.

#include <HalClock.h>

HalClock halClock;

void HalClock::begin() { _available = false; }

bool HalClock::getTime(uint8_t&, uint8_t&) const { return false; }

bool HalClock::formatTime(char*, size_t, uint8_t, bool) const { return false; }

bool HalClock::syncFromNTP() { return false; }
