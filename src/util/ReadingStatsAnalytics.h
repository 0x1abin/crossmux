#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"

namespace ReadingStatsAnalytics {

struct DayBookEntry {
  const ReadingBookStats* book = nullptr;
  uint64_t readingMs = 0;
};

struct TimelineDayEntry {
  uint32_t dayOrdinal = 0;
  uint64_t totalReadingMs = 0;
  uint32_t booksReadCount = 0;
  const ReadingBookStats* topBook = nullptr;
  uint64_t topBookReadingMs = 0;
};

std::string formatDurationHm(uint64_t totalMs);
std::string formatDayOrdinalLabel(uint32_t dayOrdinal);
std::string formatMonthLabel(int year, unsigned month);
int getReferenceYear();
std::vector<DayBookEntry> getBooksReadOnDay(uint32_t dayOrdinal);
TimelineDayEntry buildTimelineDayEntry(uint32_t dayOrdinal);
std::vector<TimelineDayEntry> buildTimelineEntries(size_t maxEntries = 0);

}  // namespace ReadingStatsAnalytics
