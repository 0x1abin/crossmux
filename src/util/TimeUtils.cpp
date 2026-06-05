#include "TimeUtils.h"

#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"

namespace {
constexpr uint32_t VALID_CLOCK_THRESHOLD = 1704067200UL;  // 2024-01-01 UTC

// Local offset in seconds derived from the status-bar clock setting.
// clockUtcOffsetQ is biased quarter-hours: 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
int32_t localOffsetSeconds() {
  int offsetQ = static_cast<int>(SETTINGS.clockUtcOffsetQ);
  if (offsetQ > 104) offsetQ = 48;
  return (offsetQ - 48) * 15 * 60;
}

// Howard Hinnant's days-from-civil algorithm (proleptic Gregorian, days since 1970-01-01).
int32_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

void civilFromDays(int z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + era * 400;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPart + 2) / 5 + 1;
  month = monthPart + (monthPart < 10 ? 3 : -9);
  year += (month <= 2);
}

// Break an epoch into the local civil date using the fixed offset.
void localCivilDate(const uint32_t epochSeconds, int& year, unsigned& month, unsigned& day) {
  const int64_t local = static_cast<int64_t>(epochSeconds) + localOffsetSeconds();
  int32_t dayOrdinal = static_cast<int32_t>(local / 86400);
  if (local < 0 && (local % 86400) != 0) {
    --dayOrdinal;  // floor toward negative infinity
  }
  civilFromDays(dayOrdinal, year, month, day);
}

std::string formatIsoDate(const int year, const unsigned month, const unsigned day, const bool appendBang) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u%s", year, month, day, appendBang ? "!" : "");
  return buffer;
}
}  // namespace

bool TimeUtils::isClockValid() { return isClockValid(static_cast<uint32_t>(time(nullptr))); }

bool TimeUtils::isClockValid(const uint32_t epochSeconds) { return epochSeconds >= VALID_CLOCK_THRESHOLD; }

uint32_t TimeUtils::getCurrentValidTimestamp() {
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  return isClockValid(now) ? now : 0;
}

uint32_t TimeUtils::getAuthoritativeTimestamp() { return getCurrentValidTimestamp(); }

uint32_t TimeUtils::getLocalDayOrdinal(const uint32_t epochSeconds) {
  if (!isClockValid(epochSeconds)) {
    return 0;
  }
  const int64_t local = static_cast<int64_t>(epochSeconds) + localOffsetSeconds();
  if (local < 0) {
    return 0;
  }
  return static_cast<uint32_t>(local / 86400);
}

uint32_t TimeUtils::getDayOrdinalForDate(const int year, const unsigned month, const unsigned day) {
  return static_cast<uint32_t>(daysFromCivil(year, month, day));
}

bool TimeUtils::getDateFromDayOrdinal(const uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day) {
  civilFromDays(static_cast<int>(dayOrdinal), year, month, day);
  return true;
}

std::string TimeUtils::formatDate(const uint32_t epochSeconds, const bool appendBang) {
  if (!isClockValid(epochSeconds)) {
    return "";
  }
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  localCivilDate(epochSeconds, year, month, day);
  return formatIsoDate(year, month, day, appendBang);
}

std::string TimeUtils::formatDateParts(const int year, const unsigned month, const unsigned day,
                                       const bool appendBang) {
  return formatIsoDate(year, month, day, appendBang);
}

std::string TimeUtils::formatMonthYear(const int year, const unsigned month) {
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%04d-%02u", year, month);
  return buffer;
}
