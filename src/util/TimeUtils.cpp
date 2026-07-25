#include "TimeUtils.h"

#include <HalClock.h>

#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"

namespace {
constexpr uint32_t VALID_CLOCK_THRESHOLD = 1704016800UL;  // 2024-01-01 at UTC+14
constexpr int MIN_MANUAL_YEAR = 2024;
constexpr int MAX_MANUAL_YEAR = 2099;

int32_t localOffsetSeconds(uint8_t encodedOffset) {
  int offsetQ = static_cast<int>(encodedOffset);
  if (offsetQ > 104) offsetQ = 48;
  return (offsetQ - 48) * 15 * 60;
}

int32_t localOffsetSeconds() { return localOffsetSeconds(SETTINGS.clockUtcOffsetQ); }

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

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

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

bool TimeUtils::isClockValid() { return halClock.hasValidTime(); }

bool TimeUtils::isClockValid(const uint32_t epochSeconds) { return epochSeconds >= VALID_CLOCK_THRESHOLD; }

uint32_t TimeUtils::getCurrentValidTimestamp() {
  const time_t now = halClock.nowUtc();
  return now > 0 && static_cast<uint64_t>(now) <= UINT32_MAX ? static_cast<uint32_t>(now) : 0;
}

uint32_t TimeUtils::getAuthoritativeTimestamp() { return getCurrentValidTimestamp(); }

bool TimeUtils::getLocalDateTime(const uint32_t epochSeconds, std::tm& out) {
  return getLocalDateTime(epochSeconds, SETTINGS.clockUtcOffsetQ, out);
}

bool TimeUtils::getLocalDateTime(const uint32_t epochSeconds, const uint8_t encodedOffset, std::tm& out) {
  const int64_t localSeconds = static_cast<int64_t>(epochSeconds) + localOffsetSeconds(encodedOffset);
  const time_t localTime = static_cast<time_t>(localSeconds);
  return static_cast<int64_t>(localTime) == localSeconds && gmtime_r(&localTime, &out);
}

unsigned TimeUtils::getDaysInMonth(const int year, const unsigned month) {
  static constexpr unsigned DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return DAYS_PER_MONTH[month - 1];
}

bool TimeUtils::localDateTimeToUtcEpoch(const int year, const unsigned month, const unsigned day, const unsigned hour,
                                        const unsigned minute, uint32_t& epochSeconds) {
  if (year < MIN_MANUAL_YEAR || year > MAX_MANUAL_YEAR || day < 1 || day > getDaysInMonth(year, month) || hour > 23 ||
      minute > 59) {
    return false;
  }

  const int64_t localSeconds = static_cast<int64_t>(daysFromCivil(year, month, day)) * 86400 +
                               static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60;
  const int64_t utcSeconds = localSeconds - localOffsetSeconds();
  if (utcSeconds < VALID_CLOCK_THRESHOLD || utcSeconds > UINT32_MAX) return false;

  std::tm roundTrip{};
  if (!getLocalDateTime(static_cast<uint32_t>(utcSeconds), roundTrip) || roundTrip.tm_year + 1900 != year ||
      roundTrip.tm_mon + 1 != static_cast<int>(month) || roundTrip.tm_mday != static_cast<int>(day) ||
      roundTrip.tm_hour != static_cast<int>(hour) || roundTrip.tm_min != static_cast<int>(minute)) {
    return false;
  }

  epochSeconds = static_cast<uint32_t>(utcSeconds);
  return true;
}

bool TimeUtils::formatTime(const uint32_t epochSeconds, const uint8_t encodedOffset, const bool use12Hour, char* buffer,
                           const size_t bufferSize) {
  if (!buffer || bufferSize < (use12Hour ? 9u : 6u) || !isClockValid(epochSeconds)) return false;

  std::tm local{};
  if (!getLocalDateTime(epochSeconds, encodedOffset, local)) return false;

  if (use12Hour) {
    const bool pm = local.tm_hour >= 12;
    const int hour12 = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    snprintf(buffer, bufferSize, "%d:%02d %s", hour12, local.tm_min, pm ? "PM" : "AM");
  } else {
    snprintf(buffer, bufferSize, "%02d:%02d", local.tm_hour, local.tm_min);
  }
  return true;
}

bool TimeUtils::formatCurrentTime(char* buffer, const size_t bufferSize, const bool use12Hour) {
  return formatTime(getCurrentValidTimestamp(), SETTINGS.clockUtcOffsetQ, use12Hour, buffer, bufferSize);
}

bool TimeUtils::formatCurrentDateTime(char* buffer, const size_t bufferSize, const bool use12Hour) {
  const uint32_t now = getCurrentValidTimestamp();
  std::tm local{};
  if (!buffer || !isClockValid(now) || !getLocalDateTime(now, local)) return false;

  char timeBuffer[9];
  if (!formatTime(now, SETTINGS.clockUtcOffsetQ, use12Hour, timeBuffer, sizeof(timeBuffer))) return false;
  return snprintf(buffer, bufferSize, "%04d-%02d-%02d %s", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  timeBuffer) > 0;
}

void TimeUtils::formatUtcOffset(const uint8_t encodedOffset, char* buffer, const size_t bufferSize) {
  int totalMinutes = (static_cast<int>(encodedOffset > 104 ? 48 : encodedOffset) - 48) * 15;
  const char sign = totalMinutes < 0 ? '-' : '+';
  if (totalMinutes < 0) totalMinutes = -totalMinutes;
  snprintf(buffer, bufferSize, "UTC%c%d:%02d", sign, totalMinutes / 60, totalMinutes % 60);
}

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
