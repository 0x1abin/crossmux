#include "StandbyTime.h"

#include <Arduino.h>

#include "../../../util/TimeUtils.h"

namespace standby_time {
namespace {

constexpr unsigned kFallbackStartHH = 16;
constexpr unsigned kFallbackStartMM = 38;

}  // namespace

bool isSynced() { return TimeUtils::isClockValid(); }

void getNowHHMM(const uint32_t fallbackStartMs, unsigned& hh, unsigned& mm) {
  std::tm localTime{};
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now && TimeUtils::getLocalDateTime(now, localTime)) {
    hh = static_cast<unsigned>(localTime.tm_hour);
    mm = static_cast<unsigned>(localTime.tm_min);
    return;
  }

  const uint32_t elapsedMin = (millis() - fallbackStartMs) / 60000u;
  const uint32_t totalMin = kFallbackStartHH * 60u + kFallbackStartMM + elapsedMin;
  hh = (totalMin / 60u) % 24u;
  mm = totalMin % 60u;
}

uint32_t getMinuteTick(const uint32_t fallbackStartMs) {
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  return now ? now / 60 : (millis() - fallbackStartMs) / 60000u;
}

}  // namespace standby_time
