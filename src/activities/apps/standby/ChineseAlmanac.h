#pragma once

#include <cstdint>
#include <ctime>

// Pure computation for the Chinese traditional almanac (老黄历). Translates a
// Gregorian instant (Beijing time, UTC+8) into the lunar date, ganzhi (天干
// 地支) for year/month/day, 24 solar terms, zodiac, and a (simplified) yi/ji
// recommendation set.
//
// No GfxRenderer / I18n / heap dependencies. All data tables are
// `static constexpr` (resident in Flash). RAM cost: zero.
//
// Range: 1900-01-01 .. 2100-12-31 (Gregorian). `computeAlmanac` returns false
// outside this window.
//
// Body is gated by `ENABLE_CHINESE_VERSION`; non-CN builds compile this TU
// into an empty object.

struct AlmanacDay {
  uint16_t gregYear;        // 1900..2100
  uint8_t  gregMonth;       // 1..12
  uint8_t  gregDay;         // 1..31
  uint8_t  weekdayIdx;      // 0=Sun .. 6=Sat
  uint16_t lunarYear;       // 1900..2100
  uint8_t  lunarMonth;      // 1..12
  uint8_t  lunarDay;        // 1..30
  bool     lunarLeap;       // true if `lunarMonth` is the leap month of the year
  uint8_t  yearStemIdx;     // 0..9   (甲乙丙丁戊己庚辛壬癸)
  uint8_t  yearBranchIdx;   // 0..11  (子丑寅卯辰巳午未申酉戌亥)
  uint8_t  monthStemIdx;    // 0..9
  uint8_t  monthBranchIdx;  // 0..11
  uint8_t  dayStemIdx;      // 0..9
  uint8_t  dayBranchIdx;    // 0..11
  uint8_t  termCurrentIdx;  // 0..23 — index into kSolarTermNames[]
                            // (0=立春, 1=雨水, ..., 23=大寒)
                            // The most recent term whose date <= today.
  uint8_t  termNextIdx;     // 0..23 — next term after `termCurrentIdx`
  uint8_t  daysToNextTerm;  // 1..30 — days until `termNextIdx` (today excluded)
  uint8_t  yiIdx;           // 0..11 — index into kYiPool (== dayBranchIdx)
  uint8_t  jiIdx;           // 0..11 — index into kJiPool (== dayBranchIdx)
  uint8_t  clashBranchIdx;  // 0..11 — branch that "clashes" with today
};

namespace chinese_almanac {

// String tables (all CJK chars guaranteed by the CN-build 8/10/12/14pt bitmap
// subset; see lib/EpdFont/scripts/cn_common_chars.txt).
extern const char* const kStemNames[10];        // "甲".."癸"
extern const char* const kBranchNames[12];      // "子".."亥"
extern const char* const kZodiacNames[12];      // "鼠".."猪"
extern const char* const kSolarTermNames[24];   // "立春".."大寒"
extern const char* const kLunarMonthNames[12];  // "正月", "二月", ..., "腊月"
extern const char* const kLunarDayNames[30];    // "初一", "初二", ..., "三十"
extern const char* const kYiPool[12][4];        // 12 sets × 4 yi items (by day branch)
extern const char* const kJiPool[12][4];        // 12 sets × 4 ji items (by day branch)

}  // namespace chinese_almanac

// Compute the almanac for the given UTC+8 instant. Caller passes a `time_t`
// already adjusted to Beijing time (i.e. `time(nullptr)` with TZ set by
// configTime(8*3600, ...) — `localtime_r` returns local Beijing struct tm).
//
// Returns false (and leaves `out` indeterminate) if the date is outside
// 1900-01-01 .. 2100-12-31.
bool computeAlmanac(const struct tm& localTime, AlmanacDay& out);
