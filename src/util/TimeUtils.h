#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

// Time helpers for the Reading Analytics suite.
//
// CrossPoint expresses local time as the system clock (set by NTP / the DS3231
// RTC) plus a fixed quarter-hour offset (CrossPointSettings::clockUtcOffsetQ).
// This adapter buckets reading time into local "day ordinals" (days since the
// Unix epoch) using that same offset, so the analytics day boundaries line up
// with the on-device clock. It deliberately does NOT call setenv("TZ")/tzset(),
// which would globally change localtime_r() and corrupt the standby clock.
namespace TimeUtils {

// True when `epochSeconds` can represent a supported local time in any offset.
bool isClockValid();
bool isClockValid(uint32_t epochSeconds);

// Current epoch seconds if the clock is trustworthy, otherwise 0.
uint32_t getCurrentValidTimestamp();
uint32_t getAuthoritativeTimestamp();

// Convert a UTC epoch to configured fixed-offset local time without changing TZ.
bool getLocalDateTime(uint32_t epochSeconds, std::tm& out);
bool getLocalDateTime(uint32_t epochSeconds, uint8_t utcOffsetQuarterHoursBiased, std::tm& out);

// Convert a local civil time in the configured fixed offset back to UTC.
bool localDateTimeToUtcEpoch(int year, unsigned month, unsigned day, unsigned hour, unsigned minute,
                             uint32_t& epochSeconds);
unsigned getDaysInMonth(int year, unsigned month);

// Caller-buffer formatting for UI hot paths.
bool formatTime(uint32_t epochSeconds, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour, char* buffer,
                size_t bufferSize);
bool formatCurrentTime(char* buffer, size_t bufferSize, bool use12Hour);
bool formatCurrentDateTime(char* buffer, size_t bufferSize, bool use12Hour);
void formatUtcOffset(uint8_t utcOffsetQuarterHoursBiased, char* buffer, size_t bufferSize);

// Local day number (days since 1970-01-01 in local time), or 0 if the clock is invalid.
uint32_t getLocalDayOrdinal(uint32_t epochSeconds);
// Civil date -> day ordinal (offset-independent).
uint32_t getDayOrdinalForDate(int year, unsigned month, unsigned day);
// Day ordinal -> civil date. Always returns true.
bool getDateFromDayOrdinal(uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day);

// Display formatting (ISO-style, locale-independent).
std::string formatDate(uint32_t epochSeconds, bool appendBang = false);
std::string formatDateParts(int year, unsigned month, unsigned day, bool appendBang = false);
std::string formatMonthYear(int year, unsigned month);

}  // namespace TimeUtils
