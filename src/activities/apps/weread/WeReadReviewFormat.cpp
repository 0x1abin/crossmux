#include "WeReadReviewFormat.h"

#include <I18n.h>

#include <cstdio>
#include <ctime>

namespace WeReadReviewFormat {

std::string rating(int starPercent) {
  // WeRead encodes the 1-5 scale as 20/40/60/80/100.
  if (starPercent < 20 || starPercent > 100) return {};
  const int stars = starPercent / 20;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%d/5", stars);
  return std::string(buf);
}

std::string date(uint32_t unixSeconds) {
  if (unixSeconds == 0) return {};
  time_t t = static_cast<time_t>(unixSeconds);
  struct tm tm{};
  gmtime_r(&t, &tm);
  char buf[12];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
  return std::string(buf);
}

std::string author(const std::string& rawAuthorName) {
  return rawAuthorName.empty() ? std::string(tr(STR_WEREAD_ANONYMOUS_READER)) : rawAuthorName;
}

}  // namespace WeReadReviewFormat
