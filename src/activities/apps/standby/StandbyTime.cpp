#include "StandbyTime.h"

#include <Arduino.h>
#include <time.h>

#include "../../../util/TimeUtils.h"

namespace standby_time {
namespace {

bool g_synced =
#ifdef CROSSPOINT_EMULATED
    true;  // Host system time is already correct; skip Wi-Fi/NTP entirely.
#else
    false;
#endif

// Pre-sync display: start at a plausible wall-clock time and tick forward so
// the clock looks alive instead of stuck at 00:00 during the few seconds
// before NTP returns (or indefinitely when Wi-Fi sync is unavailable).
constexpr unsigned kFallbackStartHH = 16;
constexpr unsigned kFallbackStartMM = 38;

}  // namespace

bool isSynced() { return g_synced; }

void setSynced(bool v) { g_synced = v; }

void getNowHHMM(uint32_t fallbackStartMs, unsigned& hh, unsigned& mm) {
  if (g_synced) {
    std::tm localTime{};
    const uint32_t now = TimeUtils::getCurrentValidTimestamp();
    if (now && TimeUtils::getLocalDateTime(now, localTime)) {
      hh = static_cast<unsigned>(localTime.tm_hour);
      mm = static_cast<unsigned>(localTime.tm_min);
      return;
    }
  }

  const uint32_t elapsedMin = (millis() - fallbackStartMs) / 60000u;
  const uint32_t totalMin = kFallbackStartHH * 60u + kFallbackStartMM + elapsedMin;
  hh = (totalMin / 60u) % 24u;
  mm = totalMin % 60u;
}

uint32_t getMinuteTick(uint32_t fallbackStartMs) {
  if (g_synced) {
    const time_t now = time(nullptr);
    return static_cast<uint32_t>(now / 60);
  }
  return (millis() - fallbackStartMs) / 60000u;
}

}  // namespace standby_time
