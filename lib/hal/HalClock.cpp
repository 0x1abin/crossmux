#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;

namespace {

// Earliest UTC instant that can represent 2024-01-01 in the supported UTC+14
// local offset.
constexpr time_t MIN_TRUSTED_EPOCH = 1704016800;  // 2023-12-31 10:00 UTC
constexpr uint16_t MIN_TRUSTED_YEAR = 2023;
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
  if (converted < MIN_TRUSTED_EPOCH || static_cast<int64_t>(converted) != seconds) return false;

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
  if (epoch < MIN_TRUSTED_EPOCH) return false;

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
  _rtcAvailable = _sdkRtc.begin();
  LOG_INF("CLK", _rtcAvailable ? "External RTC found" : "Using software clock");

  if (!hasValidTime() && restoreSystemTimeFromRtc()) {
    LOG_INF("CLK", "System UTC clock restored from external RTC");
  }
}

time_t HalClock::nowUtc() const {
  const time_t now = time(nullptr);
  return now >= MIN_TRUSTED_EPOCH ? now : 0;
}

bool HalClock::hasValidTime() const { return nowUtc() != 0; }

bool HalClock::restoreSystemTimeFromRtc() {
  if (!_rtcAvailable) return false;

  Rtc::DateTime rtcTime{};
  time_t epoch = 0;
  if (!_sdkRtc.now(rtcTime) || !rtcDateTimeToEpoch(rtcTime, epoch)) return false;

  const struct timeval systemTime = {epoch, 0};
  if (settimeofday(&systemTime, nullptr) != 0) {
    LOG_ERR("CLK", "Failed to restore system time from external RTC");
    return false;
  }
  return true;
}

bool HalClock::updateRtcFromSystemTime() {
  if (!_rtcAvailable) return true;

  const time_t now = nowUtc();
  Rtc::DateTime rtcTime{};
  if (!epochToRtcDateTime(now, rtcTime)) {
    LOG_ERR("CLK", "System time is invalid; external RTC was not updated");
    return false;
  }
  if (!_sdkRtc.set(rtcTime)) {
    LOG_ERR("CLK", "Failed to write UTC date and time to external RTC");
    return false;
  }

  Rtc::DateTime verifiedTime{};
  time_t verifiedEpoch = 0;
  if (!_sdkRtc.now(verifiedTime) || !rtcDateTimeToEpoch(verifiedTime, verifiedEpoch)) {
    LOG_ERR("CLK", "External RTC write could not be verified");
    return false;
  }
  const time_t writeSkew = verifiedEpoch >= now ? verifiedEpoch - now : now - verifiedEpoch;
  if (writeSkew > MAX_RTC_WRITE_SKEW_SECONDS) {
    LOG_ERR("CLK", "External RTC write verification differs by %lld seconds", static_cast<long long>(writeSkew));
    return false;
  }

  LOG_INF("CLK", "External RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", static_cast<unsigned>(verifiedTime.year),
          static_cast<unsigned>(verifiedTime.month), static_cast<unsigned>(verifiedTime.day),
          static_cast<unsigned>(verifiedTime.hour), static_cast<unsigned>(verifiedTime.minute),
          static_cast<unsigned>(verifiedTime.second));
  return true;
}

bool HalClock::setUtcTime(const time_t epoch) {
  if (epoch < MIN_TRUSTED_EPOCH) return false;

  const struct timeval systemTime = {epoch, 0};
  if (settimeofday(&systemTime, nullptr) != 0) {
    LOG_ERR("CLK", "Failed to set system UTC clock");
    return false;
  }

  stopSntp();
  _syncState = ClockSyncState::Idle;
  _lastSyncMs = 0;
  if (!updateRtcFromSystemTime()) {
    LOG_ERR("CLK", "System clock set, but external RTC persistence failed");
  }
  return true;
}

bool HalClock::startSntp() {
  if (_syncState == ClockSyncState::Syncing) return true;

  if (WiFi.status() != WL_CONNECTED) {
    _syncState = ClockSyncState::Failed;
    return false;
  }

  if (!_sntpInitialized) {
#ifdef ENABLE_CHINESE_VERSION
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org"));
#else
    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.nist.gov"));
#endif
    config.start = false;
    config.smooth_sync = false;
    if (esp_netif_sntp_init(&config) != ESP_OK) {
      LOG_ERR("CLK", "Failed to initialize SNTP service");
      _syncState = ClockSyncState::Failed;
      return false;
    }
    _sntpInitialized = true;
  }

  if (esp_netif_sntp_start() != ESP_OK) {
    LOG_ERR("CLK", "Failed to start SNTP service");
    _syncState = ClockSyncState::Failed;
    return false;
  }

  _syncState = ClockSyncState::Syncing;
  LOG_INF("CLK", "SNTP sync started");
  return true;
}

void HalClock::stopSntp() {
  if (!_sntpInitialized) return;
  esp_netif_sntp_deinit();
  _sntpInitialized = false;
}

void HalClock::completeSync() {
  if (!hasValidTime()) {
    LOG_ERR("CLK", "SNTP completed without a trustworthy system clock");
    _syncState = ClockSyncState::Failed;
    stopSntp();
    return;
  }

  _syncState = ClockSyncState::Succeeded;
  _lastSyncMs = millis();
  stopSntp();
  if (!updateRtcFromSystemTime()) {
    LOG_ERR("CLK", "System clock synced, but external RTC persistence failed");
  }
  LOG_INF("CLK", "System UTC clock synchronized");
}

bool HalClock::requestSync() { return startSntp(); }

bool HalClock::syncNow(const uint32_t timeoutMs) {
  if (!startSntp()) return false;

  if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeoutMs)) != ESP_OK) {
    LOG_ERR("CLK", "SNTP sync timed out");
    _syncState = ClockSyncState::Failed;
    if (!_autoSyncEnabled) stopSntp();
    return false;
  }

  completeSync();
  if (!_autoSyncEnabled) stopSntp();
  return _syncState == ClockSyncState::Succeeded;
}

void HalClock::setAutoSyncEnabled(const bool enabled) {
  if (enabled && !_autoSyncEnabled) _wifiWasConnected = false;
  _autoSyncEnabled = enabled;
  if (!enabled) {
    stopSntp();
    if (_syncState == ClockSyncState::Syncing) _syncState = ClockSyncState::Idle;
  }
}

void HalClock::update() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (_syncState == ClockSyncState::Syncing) _syncState = ClockSyncState::Failed;
    if (_wifiWasConnected) stopSntp();
    _wifiWasConnected = false;
    return;
  }

  if (_sntpInitialized && esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    completeSync();
  }

  const bool fresh = _syncState == ClockSyncState::Succeeded && millis() - _lastSyncMs < CONFIG_LWIP_SNTP_UPDATE_DELAY;
  const bool syncDue = !_wifiWasConnected || (_syncState == ClockSyncState::Succeeded && !fresh);
  if (_autoSyncEnabled && !_sntpInitialized && syncDue) {
    startSntp();
  }
  _wifiWasConnected = true;
}
