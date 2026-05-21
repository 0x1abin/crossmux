// HAL clock stub for the simulator. The real HalClock drives a DS3231 RTC over I2C
// (an X3-only peripheral) and syncs it from NTP. The host has no such RTC, so this
// reports the clock as unavailable — exactly how the firmware behaves on X4 hardware,
// where the status-bar clock and clock settings entries stay hidden.

#include <HalClock.h>

HalClock halClock;

void HalClock::begin() {
  // No DS3231 RTC on the host; leave _available false so clock UI is suppressed.
  _available = false;
}

bool HalClock::getTime(uint8_t& /*hour*/, uint8_t& /*minute*/) const { return false; }

bool HalClock::formatTime(char* /*buf*/, size_t /*bufSize*/, uint8_t /*utcOffsetQuarterHoursBiased*/,
                          bool /*use12Hour*/) const {
  return false;
}

bool HalClock::syncFromNTP() { return false; }
