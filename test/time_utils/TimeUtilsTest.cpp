#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
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
      {0, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 1, 22, 30},
      {104, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 3, 0, 30},
      {71, utcEpoch(2025, 1, 2, 18, 30), 2025, 1, 3, 0, 15},
      {255, utcEpoch(2025, 1, 2, 10, 30), 2025, 1, 2, 10, 30},
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
