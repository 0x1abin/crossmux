#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

namespace {

constexpr uint16_t MIN_TRUSTED_YEAR = 2024;
constexpr uint16_t MAX_RTC_YEAR = 2099;
constexpr time_t MAX_RTC_WRITE_SKEW_SECONDS = 2;

int32_t daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

bool rtcDateTimeToEpoch(const Rtc::DateTime& rtcTime, time_t& epoch) {
  if (rtcTime.year < MIN_TRUSTED_YEAR || rtcTime.year > MAX_RTC_YEAR || rtcTime.month < 1 || rtcTime.month > 12 ||
      rtcTime.day < 1 || rtcTime.day > 31 || rtcTime.hour > 23 || rtcTime.minute > 59 || rtcTime.second > 59) {
    return false;
  }

  const int64_t seconds = static_cast<int64_t>(daysFromCivil(rtcTime.year, rtcTime.month, rtcTime.day)) * 86400 +
                          static_cast<int64_t>(rtcTime.hour) * 3600 + static_cast<int64_t>(rtcTime.minute) * 60 +
                          rtcTime.second;
  const time_t converted = static_cast<time_t>(seconds);
  if (converted < 0 || static_cast<int64_t>(converted) != seconds) return false;

  struct tm roundTrip{};
  if (!gmtime_r(&converted, &roundTrip) || roundTrip.tm_year != static_cast<int>(rtcTime.year) - 1900 ||
      roundTrip.tm_mon != static_cast<int>(rtcTime.month) - 1 || roundTrip.tm_mday != rtcTime.day ||
      roundTrip.tm_hour != rtcTime.hour || roundTrip.tm_min != rtcTime.minute || roundTrip.tm_sec != rtcTime.second) {
    return false;
  }

  epoch = converted;
  return true;
}

bool epochToRtcDateTime(const time_t epoch, Rtc::DateTime& rtcTime) {
  if (epoch < 0) return false;

  struct tm utcTime{};
  if (!gmtime_r(&epoch, &utcTime)) return false;

  const int year = utcTime.tm_year + 1900;
  if (year < MIN_TRUSTED_YEAR || year > MAX_RTC_YEAR) return false;

  rtcTime.year = static_cast<uint16_t>(year);
  rtcTime.month = static_cast<uint8_t>(utcTime.tm_mon + 1);
  rtcTime.day = static_cast<uint8_t>(utcTime.tm_mday);
  rtcTime.hour = static_cast<uint8_t>(utcTime.tm_hour);
  rtcTime.minute = static_cast<uint8_t>(utcTime.tm_min);
  rtcTime.second = static_cast<uint8_t>(utcTime.tm_sec);
  rtcTime.weekday = static_cast<uint8_t>(utcTime.tm_wday);
  return true;
}

}  // namespace

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "RTC found" : "RTC not found");
  if (!_available) return;

  if (restoreSystemTimeFromRtc()) {
    LOG_INF("CLK", "System UTC clock restored from RTC");
    return;
  }

  LOG_INF("CLK", "RTC calendar is unavailable or not trustworthy");
  uint8_t hour = 0;
  uint8_t minute = 0;
  getTime(hour, minute);
}

bool HalClock::hasValidRtcTime() const {
  if (!_available) return false;
  Rtc::DateTime rtcTime{};
  time_t epoch = 0;
  return _sdkRtc.now(rtcTime) && rtcDateTimeToEpoch(rtcTime, epoch);
}

bool HalClock::restoreSystemTimeFromRtc() {
  if (!_available) return false;

  Rtc::DateTime rtcTime{};
  time_t epoch = 0;
  if (!_sdkRtc.now(rtcTime) || !rtcDateTimeToEpoch(rtcTime, epoch)) return false;

  const struct timeval systemTime = {epoch, 0};
  if (settimeofday(&systemTime, nullptr) != 0) {
    LOG_ERR("CLK", "Failed to restore system time from RTC");
    return false;
  }

  _cachedHour = rtcTime.hour;
  _cachedMinute = rtcTime.minute;
  _hasCachedTime = true;
  _lastPollMs = millis();
  return true;
}

bool HalClock::updateRtcFromSystemTime() {
  if (!_available) return false;

  const time_t now = time(nullptr);
  Rtc::DateTime rtcTime{};
  if (!epochToRtcDateTime(now, rtcTime)) {
    LOG_ERR("CLK", "System time is invalid; RTC was not updated");
    return false;
  }
  if (!_sdkRtc.set(rtcTime)) {
    LOG_ERR("CLK", "Failed to write UTC date and time to RTC");
    return false;
  }

  Rtc::DateTime verifiedTime{};
  time_t verifiedEpoch = 0;
  if (!_sdkRtc.now(verifiedTime) || !rtcDateTimeToEpoch(verifiedTime, verifiedEpoch)) {
    LOG_ERR("CLK", "RTC write could not be verified");
    return false;
  }
  const time_t writeSkew = verifiedEpoch >= now ? verifiedEpoch - now : now - verifiedEpoch;
  if (writeSkew > MAX_RTC_WRITE_SKEW_SECONDS) {
    LOG_ERR("CLK", "RTC write verification differs by %lld seconds", static_cast<long long>(writeSkew));
    return false;
  }

  _cachedHour = verifiedTime.hour;
  _cachedMinute = verifiedTime.minute;
  _hasCachedTime = true;
  _lastPollMs = 0;
  LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", static_cast<unsigned>(verifiedTime.year),
          static_cast<unsigned>(verifiedTime.month), static_cast<unsigned>(verifiedTime.day),
          static_cast<unsigned>(verifiedTime.hour), static_cast<unsigned>(verifiedTime.minute),
          static_cast<unsigned>(verifiedTime.second));
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Rtc::DateTime dt{};
  if (!_sdkRtc.now(dt) || dt.hour > 23 || dt.minute > 59) {
    if (!_hasCachedTime) return false;
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _lastPollMs = now;
  _hasCachedTime = true;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      return updateRtcFromSystemTime();
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}
