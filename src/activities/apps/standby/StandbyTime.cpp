#include "StandbyTime.h"

#include <Arduino.h>

#include "../../../util/TimeUtils.h"

namespace standby_time {
namespace {

constexpr unsigned kFallbackStartHH = 16;
constexpr unsigned kFallbackStartMM = 38;

}  // namespace

bool isSynced() { return TimeUtils::isClockValid(); }

bool shouldLightSleep(const uint32_t idleMs, const bool syncActive, const bool wifiActive, const bool usbConnected,
                      const bool hardwareAllowed) {
  return hardwareAllowed && idleMs >= kLightSleepIdleMs && !syncActive && !wifiActive && !usbConnected;
}

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

uint8_t getTenMinuteBucket(const uint32_t fallbackStartMs) {
  unsigned hh = 0;
  unsigned mm = 0;
  getNowHHMM(fallbackStartMs, hh, mm);
  return static_cast<uint8_t>(hh * 6u + mm / 10u);
}

bool didTenMinuteBucketChange(const int16_t previousBucket, const uint8_t currentBucket) {
  return previousBucket >= 0 && previousBucket != currentBucket;
}

}  // namespace standby_time
