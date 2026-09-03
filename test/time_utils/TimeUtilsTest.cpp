#include <Arduino.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
#include "HalClock.h"
#include "StandbyTime.h"
#include "TimeUtils.h"

namespace {

uint32_t utcEpoch(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
  return TimeUtils::getDayOrdinalForDate(year, month, day) * 86400u + hour * 3600u + minute * 60u;
}

struct LocalTimeCase {
  uint8_t offsetQ;
  uint32_t epoch;
  int year;
  int month;
  int day;
  int hour;
  int minute;
};

}  // namespace

TEST(TimeUtils, FixedOffsetConversionIgnoresProcessTimezone) {
  const char* oldTz = getenv("TZ");
  const bool hadTz = oldTz != nullptr;
  const std::string savedTz = hadTz ? oldTz : "";
  setenv("TZ", "EST5EDT", 1);
  tzset();

  const LocalTimeCase cases[] = {
      {0, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 1, 22, 30}, {104, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 3, 0, 30},
      {71, utcEpoch(2025, 1, 2, 18, 30), 2025, 1, 3, 0, 15}, {49, utcEpoch(2025, 1, 2, 23, 50), 2025, 1, 3, 0, 5},
      {47, utcEpoch(2025, 1, 3, 0, 5), 2025, 1, 2, 23, 50},  {255, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 2, 10, 30},
  };

  for (const auto& test : cases) {
    SETTINGS.clockUtcOffsetQ = test.offsetQ;
    std::tm local{};
    EXPECT_TRUE(TimeUtils::getLocalDateTime(test.epoch, local));
    EXPECT_EQ(local.tm_year + 1900, test.year);
    EXPECT_EQ(local.tm_mon + 1, test.month);
    EXPECT_EQ(local.tm_mday, test.day);
    EXPECT_EQ(local.tm_hour, test.hour);
    EXPECT_EQ(local.tm_min, test.minute);
  }

  if (hadTz) {
    setenv("TZ", savedTz.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
}

TEST(TimeUtils, LocalDateTimeRoundTripsAcrossOffsets) {
  const uint8_t offsets[] = {0, 47, 48, 49, 71, 104};
  for (const uint8_t offset : offsets) {
    SETTINGS.clockUtcOffsetQ = offset;
    uint32_t epoch = 0;
    ASSERT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2024, 2, 29, 23, 45, epoch));

    std::tm local{};
    ASSERT_TRUE(TimeUtils::getLocalDateTime(epoch, local));
    EXPECT_EQ(local.tm_year + 1900, 2024);
    EXPECT_EQ(local.tm_mon + 1, 2);
    EXPECT_EQ(local.tm_mday, 29);
    EXPECT_EQ(local.tm_hour, 23);
    EXPECT_EQ(local.tm_min, 45);
  }
}

TEST(TimeUtils, ManualDateValidationHandlesLeapYearsAndBounds) {
  uint32_t epoch = 0;
  SETTINGS.clockUtcOffsetQ = 48;
  EXPECT_TRUE(TimeUtils::localDateTimeToUtcEpoch(2024, 2, 29, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 2, 29, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2023, 12, 31, 23, 59, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2100, 1, 1, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 13, 1, 0, 0, epoch));
  EXPECT_FALSE(TimeUtils::localDateTimeToUtcEpoch(2025, 1, 1, 24, 0, epoch));
  EXPECT_EQ(TimeUtils::getDaysInMonth(2024, 2), 29u);
  EXPECT_EQ(TimeUtils::getDaysInMonth(2025, 2), 28u);
}

TEST(TimeUtils, FormatsTwelveAndTwentyFourHourClock) {
  const uint32_t midnight = utcEpoch(2025, 6, 1, 0, 5);
  const uint32_t afternoon = utcEpoch(2025, 6, 1, 13, 7);
  char buffer[16];

  ASSERT_TRUE(TimeUtils::formatTime(midnight, 48, false, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "00:05");
  ASSERT_TRUE(TimeUtils::formatTime(midnight, 48, true, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "12:05 AM");
  ASSERT_TRUE(TimeUtils::formatTime(afternoon, 48, true, buffer, sizeof(buffer)));
  EXPECT_STREQ(buffer, "1:07 PM");
}

TEST(TimeUtils, InvalidCurrentClockDoesNotFormat) {
  char buffer[16];
  halClock.now = 0;
  EXPECT_FALSE(TimeUtils::isClockValid());
  EXPECT_EQ(TimeUtils::getCurrentValidTimestamp(), 0u);
  EXPECT_FALSE(TimeUtils::formatCurrentTime(buffer, sizeof(buffer), false));

  halClock.now = utcEpoch(2025, 1, 2, 3, 4);
  SETTINGS.clockUtcOffsetQ = 71;
  EXPECT_TRUE(TimeUtils::isClockValid());
  ASSERT_TRUE(TimeUtils::formatCurrentTime(buffer, sizeof(buffer), false));
  EXPECT_STREQ(buffer, "08:49");
}

TEST(StandbyTime, UsesTickingFallbackUntilClockBecomesValid) {
  unsigned hour = 0;
  unsigned minute = 0;
  halClock.now = 0;
  arduinoTestMillis = 100000u;

  standby_time::getNowHHMM(arduinoTestMillis, hour, minute);
  EXPECT_EQ(hour, 16u);
  EXPECT_EQ(minute, 38u);
  EXPECT_EQ(standby_time::getMinuteTick(arduinoTestMillis), 0u);

  arduinoTestMillis += 60000u;
  standby_time::getNowHHMM(100000u, hour, minute);
  EXPECT_EQ(hour, 16u);
  EXPECT_EQ(minute, 39u);
  EXPECT_EQ(standby_time::getMinuteTick(100000u), 1u);

  halClock.now = utcEpoch(2025, 1, 2, 3, 4);
  SETTINGS.clockUtcOffsetQ = 80;
  standby_time::getNowHHMM(100000u, hour, minute);
  EXPECT_EQ(hour, 11u);
  EXPECT_EQ(minute, 4u);
}

TEST(StandbyTime, LightSleepRequiresIdleAndInactivePeripherals) {
  EXPECT_FALSE(standby_time::shouldLightSleep(34999u, false, false, false, true));
  EXPECT_TRUE(standby_time::shouldLightSleep(35000u, false, false, false, true));
  EXPECT_FALSE(standby_time::shouldLightSleep(35000u, true, false, false, true));
  EXPECT_FALSE(standby_time::shouldLightSleep(35000u, false, true, false, true));
  EXPECT_FALSE(standby_time::shouldLightSleep(35000u, false, false, true, true));
  EXPECT_FALSE(standby_time::shouldLightSleep(35000u, false, false, false, false));
}

TEST(StandbyTime, DetectsTenMinuteDisplayBucketChanges) {
  halClock.now = 0;
  constexpr uint32_t startMs = 100000u;
  arduinoTestMillis = startMs;
  const auto bucketAtMinute = [](const uint32_t elapsedMinutes) {
    arduinoTestMillis = startMs + elapsedMinutes * 60000u;
    return standby_time::getTenMinuteBucket(startMs);
  };

  const uint8_t bucket38 = bucketAtMinute(0);

  EXPECT_FALSE(standby_time::didTenMinuteBucketChange(-1, bucket38));

  const uint8_t bucket39 = bucketAtMinute(1);
  EXPECT_EQ(bucket39, bucket38);
  EXPECT_FALSE(standby_time::didTenMinuteBucketChange(bucket38, bucket39));

  const uint8_t bucket40 = bucketAtMinute(2);
  EXPECT_NE(bucket40, bucket39);
  EXPECT_TRUE(standby_time::didTenMinuteBucketChange(bucket39, bucket40));

  const uint8_t bucket19 = bucketAtMinute(41);
  const uint8_t bucket20 = bucketAtMinute(42);
  EXPECT_TRUE(standby_time::didTenMinuteBucketChange(bucket19, bucket20));

  const uint8_t bucket59 = bucketAtMinute(21);
  const uint8_t bucket00 = bucketAtMinute(22);
  EXPECT_TRUE(standby_time::didTenMinuteBucketChange(bucket59, bucket00));

  const uint8_t delayedBucket = bucketAtMinute(55);
  EXPECT_TRUE(standby_time::didTenMinuteBucketChange(bucket20, delayedBucket));
}
