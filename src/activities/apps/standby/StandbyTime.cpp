#include "StandbyTime.h"

#include "../../../util/TimeUtils.h"

namespace standby_time {

bool isSynced() { return TimeUtils::isClockValid(); }

bool getNowHHMM(unsigned& hh, unsigned& mm) {
  std::tm localTime{};
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (!now || !TimeUtils::getLocalDateTime(now, localTime)) return false;
  hh = static_cast<unsigned>(localTime.tm_hour);
  mm = static_cast<unsigned>(localTime.tm_min);
  return true;
}

uint32_t getMinuteTick() {
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  return now ? now / 60 : 0;
}

}  // namespace standby_time
