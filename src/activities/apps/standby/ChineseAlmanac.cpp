#include "ChineseAlmanac.h"

#ifdef ENABLE_CHINESE_VERSION

#include <cstring>

namespace chinese_almanac {

// =====================================================================
//  String tables — every glyph below must be present in the CN bitmap
//  font subset (lib/EpdFont/scripts/cn_common_chars.txt).
// =====================================================================

const char* const kStemNames[10] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
};

const char* const kBranchNames[12] = {
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥",
};

const char* const kZodiacNames[12] = {
    "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪",
};

// 24 solar terms, ordered starting from 立春.
const char* const kSolarTermNames[24] = {
    "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满",
    "芒种", "夏至", "小暑", "大暑", "立秋", "处暑", "白露", "秋分",
    "寒露", "霜降", "立冬", "小雪", "大雪", "冬至", "小寒", "大寒",
};

const char* const kLunarMonthNames[12] = {
    "正月", "二月", "三月", "四月", "五月", "六月",
    "七月", "八月", "九月", "十月", "冬月", "腊月",
};

const char* const kLunarDayNames[30] = {
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

// Yi/ji pools indexed by today's earthly branch (0..11). Activities are kept
// to 2 CJK chars so they fit the 2×2 grid cells in the calendar face layout.
// All chars are verified to be in the CN bitmap subset.
const char* const kYiPool[12][4] = {
    /* 子 */ {"祭典", "祈福", "出行", "入学"},
    /* 丑 */ {"交易", "纳财", "开市", "立券"},
    /* 寅 */ {"出行", "求医", "治病", "入学"},
    /* 卯 */ {"会友", "美容", "沐浴", "结网"},
    /* 辰 */ {"开市", "立券", "交易", "纳财"},
    /* 巳 */ {"入学", "祭典", "求医", "祈福"},
    /* 午 */ {"祭典", "祈福", "出行", "纳采"},
    /* 未 */ {"嫁娶", "纳采", "结网", "纳财"},
    /* 申 */ {"开市", "出行", "纳财", "立券"},
    /* 酉 */ {"嫁娶", "祭典", "立券", "纳财"},
    /* 戌 */ {"祭典", "祈福", "出行", "沐浴"},
    /* 亥 */ {"嫁娶", "纳采", "入学", "求医"},
};

const char* const kJiPool[12][4] = {
    /* 子 */ {"动土", "嫁娶", "安葬", "破土"},
    /* 丑 */ {"破土", "安葬", "嫁娶", "造作"},
    /* 寅 */ {"嫁娶", "安葬", "动土", "破土"},
    /* 卯 */ {"动土", "破土", "嫁娶", "安葬"},
    /* 辰 */ {"破土", "安葬", "造作", "动土"},
    /* 巳 */ {"安葬", "动土", "破土", "造作"},
    /* 午 */ {"造作", "动土", "安葬", "嫁娶"},
    /* 未 */ {"破土", "动土", "安葬", "出行"},
    /* 申 */ {"嫁娶", "安葬", "造作", "破土"},
    /* 酉 */ {"动土", "破土", "造作", "出行"},
    /* 戌 */ {"嫁娶", "动土", "安葬", "破土"},
    /* 亥 */ {"动土", "破土", "安葬", "造作"},
};

}  // namespace chinese_almanac

// =====================================================================
//  Lunar year info table — 1900..2100 inclusive.
//
//  Encoding (per year, uint32_t, ~20 bits used):
//    bits 3..0   : leap month (0 = no leap, 1..12 = leap-after-monthN)
//    bits 15..4  : 12 bits, M1..M12 lengths
//                  bit 15 = M1, bit 14 = M2, ..., bit 4 = M12.
//                  1 = 30 days (大月), 0 = 29 days (小月).
//    bit 16      : leap month length (1 = 30 days). Only meaningful when
//                  leap month != 0.
//    bits 31..17 : unused.
//
//  Anchor: lunar 1900 正月初一 = Gregorian 1900-01-31 (Wed).
//
//  Source: 中科院紫金山天文台 published 农历 (also reproduced verbatim in
//  many open-source projects, e.g. https://github.com/jjonline/calendar.js,
//  lunar-python, etc.).
// =====================================================================

static constexpr uint32_t kLunarInfo[201] = {
    // 1900 - 1909
    0x04bd8u, 0x04ae0u, 0x0a570u, 0x054d5u, 0x0d260u, 0x0d950u, 0x16554u,
    0x056a0u, 0x09ad0u, 0x055d2u,
    // 1910 - 1919
    0x04ae0u, 0x0a5b6u, 0x0a4d0u, 0x0d250u, 0x1d255u, 0x0b540u, 0x0d6a0u,
    0x0ada2u, 0x095b0u, 0x14977u,
    // 1920 - 1929
    0x04970u, 0x0a4b0u, 0x0b4b5u, 0x06a50u, 0x06d40u, 0x1ab54u, 0x02b60u,
    0x09570u, 0x052f2u, 0x04970u,
    // 1930 - 1939
    0x06566u, 0x0d4a0u, 0x0ea50u, 0x06e95u, 0x05ad0u, 0x02b60u, 0x186e3u,
    0x092e0u, 0x1c8d7u, 0x0c950u,
    // 1940 - 1949
    0x0d4a0u, 0x1d8a6u, 0x0b550u, 0x056a0u, 0x1a5b4u, 0x025d0u, 0x092d0u,
    0x0d2b2u, 0x0a950u, 0x0b557u,
    // 1950 - 1959
    0x06ca0u, 0x0b550u, 0x15355u, 0x04da0u, 0x0a5b0u, 0x14573u, 0x052b0u,
    0x0a9a8u, 0x0e950u, 0x06aa0u,
    // 1960 - 1969
    0x0aea6u, 0x0ab50u, 0x04b60u, 0x0aae4u, 0x0a570u, 0x05260u, 0x0f263u,
    0x0d950u, 0x05b57u, 0x056a0u,
    // 1970 - 1979
    0x096d0u, 0x04dd5u, 0x04ad0u, 0x0a4d0u, 0x0d4d4u, 0x0d250u, 0x0d558u,
    0x0b540u, 0x0b6a0u, 0x195a6u,
    // 1980 - 1989
    0x095b0u, 0x049b0u, 0x0a974u, 0x0a4b0u, 0x0b27au, 0x06a50u, 0x06d40u,
    0x0af46u, 0x0ab60u, 0x09570u,
    // 1990 - 1999
    0x04af5u, 0x04970u, 0x064b0u, 0x074a3u, 0x0ea50u, 0x06b58u, 0x055c0u,
    0x0ab60u, 0x096d5u, 0x092e0u,
    // 2000 - 2009
    0x0c960u, 0x0d954u, 0x0d4a0u, 0x0da50u, 0x07552u, 0x056a0u, 0x0abb7u,
    0x025d0u, 0x092d0u, 0x0cab5u,
    // 2010 - 2019
    0x0a950u, 0x0b4a0u, 0x0baa4u, 0x0ad50u, 0x055d9u, 0x04ba0u, 0x0a5b0u,
    0x15176u, 0x052b0u, 0x0a930u,
    // 2020 - 2029
    0x07954u, 0x06aa0u, 0x0ad50u, 0x05b52u, 0x04b60u, 0x0a6e6u, 0x0a4e0u,
    0x0d260u, 0x0ea65u, 0x0d530u,
    // 2030 - 2039
    0x05aa0u, 0x076a3u, 0x096d0u, 0x04afbu, 0x04ad0u, 0x0a4d0u, 0x1d0b6u,
    0x0d250u, 0x0d520u, 0x0dd45u,
    // 2040 - 2049
    0x0b5a0u, 0x056d0u, 0x055b2u, 0x049b0u, 0x0a577u, 0x0a4b0u, 0x0aa50u,
    0x1b255u, 0x06d20u, 0x0ada0u,
    // 2050 - 2059
    0x14b63u, 0x09370u, 0x049f8u, 0x04970u, 0x064b0u, 0x168a6u, 0x0ea50u,
    0x06b20u, 0x1a6c4u, 0x0aae0u,
    // 2060 - 2069
    0x0a2e0u, 0x0d2e3u, 0x0c960u, 0x0d557u, 0x0d4a0u, 0x0da50u, 0x05d55u,
    0x056a0u, 0x0a6d0u, 0x055d4u,
    // 2070 - 2079
    0x052d0u, 0x0a9b8u, 0x0a950u, 0x0b4a0u, 0x0b6a6u, 0x0ad50u, 0x055a0u,
    0x0aba4u, 0x0a5b0u, 0x052b0u,
    // 2080 - 2089
    0x0b273u, 0x06930u, 0x07337u, 0x06aa0u, 0x0ad50u, 0x14b55u, 0x04b60u,
    0x0a570u, 0x054e4u, 0x0d160u,
    // 2090 - 2099
    0x0e968u, 0x0d520u, 0x0daa0u, 0x16aa6u, 0x056d0u, 0x04ae0u, 0x0a9d4u,
    0x0a2d0u, 0x0d150u, 0x0f252u,
    // 2100
    0x0d520u,
};

// =====================================================================
//  Solar-term constants (寿星算法 / 紫金山天文台). For year y:
//      day_of_month(term) = floor(Y * D + C[term]) - Y/4
//  with D = 0.2422, Y = y % 100. C tables are slightly different per
//  century. Accuracy: ±0 days for ~99% of years in 1900-2100; ±1 day in
//  rare edge cases (acceptable for display).
// =====================================================================

static constexpr double kSolarTermC20[24] = {
    // century 20 (1900-1999) — values from 寿星天文历
    4.6295,  19.4599,  6.3826,  21.4155, 5.5900,  20.8880,
    6.3180,  21.8600,  6.5000,  22.2000, 7.9280,  23.6500,
    8.3500,  23.9500,  8.4400,  23.8220, 9.0980,  24.2180,
    7.2180,  22.3600,  7.9000,  22.6000, 6.1100,  20.8400,
};

static constexpr double kSolarTermC21[24] = {
    // century 21 (2000-2099) — slightly shifted from C20 due to leap-day
    // accumulation.
    3.8700,  18.7300,  5.6300,  20.6460, 4.8100,  20.1000,
    5.5200,  21.0400,  5.6780,  21.3700, 7.1080,  22.8300,
    7.5000,  23.1300,  7.6460,  23.0420, 8.3180,  23.4380,
    7.4380,  22.3600,  7.1800,  21.9400, 5.4055,  20.1200,
};

// kSolarTermMonth[i] = Gregorian month (1-12) in which term i normally falls.
// Used to anchor the formula to a calendar date.
static constexpr uint8_t kSolarTermMonth[24] = {
    2, 2,  // 立春, 雨水
    3, 3,  // 惊蛰, 春分
    4, 4,  // 清明, 谷雨
    5, 5,  // 立夏, 小满
    6, 6,  // 芒种, 夏至
    7, 7,  // 小暑, 大暑
    8, 8,  // 立秋, 处暑
    9, 9,  // 白露, 秋分
   10,10,  // 寒露, 霜降
   11,11,  // 立冬, 小雪
   12,12,  // 大雪, 冬至
    1, 1,  // 小寒, 大寒  (in calendar year y+1 relative to 立春-anchored year)
};

namespace {

// ---- Pure Gregorian helpers --------------------------------------------

constexpr bool isLeapGregorian(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

constexpr uint8_t kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

uint8_t daysInGregMonth(int y, int m) {
  if (m == 2 && isLeapGregorian(y)) return 29;
  return kDaysInMonth[m - 1];
}

// Absolute day index = days since 1900-01-01 (which is index 0, a Monday).
// Range: 0 (1900-01-01) .. 73414 (2100-12-31).
int32_t gregorianToAbsDay(int y, int m, int d) {
  // Sum full years before y.
  int32_t days = 0;
  for (int yy = 1900; yy < y; ++yy) {
    days += isLeapGregorian(yy) ? 366 : 365;
  }
  for (int mm = 1; mm < m; ++mm) {
    days += daysInGregMonth(y, mm);
  }
  days += d - 1;
  return days;
}

// Convert weekday: 1900-01-01 was a Monday → absDay 0 = Monday = weekday 1 (Sun=0).
uint8_t weekdayFromAbsDay(int32_t absDay) {
  // (absDay + 1) % 7 → Sun=0, Mon=1, ...
  int v = static_cast<int>((absDay % 7 + 8) % 7);
  return static_cast<uint8_t>(v);
}

// ---- Solar term helpers ------------------------------------------------

int solarTermDay(int year, int termIdx) {
  // Term 22 (小寒) and 23 (大寒) live in January; they belong to the
  // calendar year `year` but their formula uses Y = year % 100 with the
  // same century rule.
  int Y = year % 100;
  const double* C = (year >= 2000) ? kSolarTermC21 : kSolarTermC20;
  // For year 2100 we extrapolate using century-21 constants. Edge errors
  // of ±1 day are acceptable for display.
  double v = static_cast<double>(Y) * 0.2422 + C[termIdx] - static_cast<double>(Y / 4);
  return static_cast<int>(v);
}

// Absolute day of solar term `termIdx` in calendar `year`.
int32_t termAbsDay(int year, int termIdx) {
  return gregorianToAbsDay(year, kSolarTermMonth[termIdx], solarTermDay(year, termIdx));
}

// ---- Lunar conversion --------------------------------------------------

// Total days in lunar year (1900..2100).
int lunarYearDays(int year) {
  const uint32_t info = kLunarInfo[year - 1900];
  int days = 348;  // 12 * 29
  for (uint32_t bit = 0x8000u; bit > 0x8u; bit >>= 1) {
    if (info & bit) ++days;
  }
  const int leap = static_cast<int>(info & 0xfu);
  if (leap != 0) {
    days += (info & 0x10000u) ? 30 : 29;
  }
  return days;
}

// Days in lunar month m (1..12) of `year`. Caller guarantees no-leap path.
int lunarMonthDays(int year, int m) {
  const uint32_t info = kLunarInfo[year - 1900];
  const uint32_t bit = 0x10000u >> m;  // m=1 → bit 15, m=12 → bit 4
  return (info & bit) ? 30 : 29;
}

// Days in the leap month of `year` (0 if no leap month).
int lunarLeapDays(int year) {
  const uint32_t info = kLunarInfo[year - 1900];
  if ((info & 0xfu) == 0) return 0;
  return (info & 0x10000u) ? 30 : 29;
}

// Leap month (1..12, or 0 if none).
int lunarLeapMonth(int year) { return static_cast<int>(kLunarInfo[year - 1900] & 0xfu); }

// Returns absolute Gregorian day index of 春节 (lunar year `year` 正月初一).
int32_t springFestivalAbsDay(int year) {
  // Anchor: 1900-01-31 = lunar 1900-01-01 → absDay = 30.
  int32_t abs = 30;
  for (int y = 1900; y < year; ++y) {
    abs += lunarYearDays(y);
  }
  return abs;
}

struct LunarYMD {
  uint16_t year;
  uint8_t  month;
  uint8_t  day;
  bool     leap;
};

// Walk lunar months from 春节 of containing lunar year to find month/day.
// Pre: 1900 春节 (1900-01-31) <= absDay <= 2100 年末.
LunarYMD lunarFromAbsDay(int32_t absDay) {
  // Find lunar year whose 春节 day <= absDay < next 春节 day.
  int year = 1900;
  int32_t yearStart = 30;  // 1900 春节
  while (year < 2100) {
    const int32_t nextStart = yearStart + lunarYearDays(year);
    if (absDay < nextStart) break;
    yearStart = nextStart;
    ++year;
  }
  int32_t offset = absDay - yearStart;  // days into this lunar year

  const int leap = lunarLeapMonth(year);
  const int leapDays = lunarLeapDays(year);

  // Walk months 1..12 (insert leap after month `leap`).
  int month = 1;
  bool isLeap = false;
  while (true) {
    int monthDays;
    if (isLeap) {
      monthDays = leapDays;
    } else {
      monthDays = lunarMonthDays(year, month);
    }
    if (offset < monthDays) {
      LunarYMD ymd;
      ymd.year = static_cast<uint16_t>(year);
      ymd.month = static_cast<uint8_t>(month);
      ymd.day = static_cast<uint8_t>(offset + 1);
      ymd.leap = isLeap;
      return ymd;
    }
    offset -= monthDays;
    // Advance: if just finished month `leap` regular, next is leap month.
    if (!isLeap && month == leap) {
      isLeap = true;
    } else {
      isLeap = false;
      ++month;
      if (month > 12) {
        // Should not happen unless absDay extends past year end; safety net.
        LunarYMD ymd{static_cast<uint16_t>(year), 12, 30, false};
        return ymd;
      }
    }
  }
}

// ---- Ganzhi (天干地支) -------------------------------------------------

// Year stem/branch — switches at 立春 (term 0), not 春节.
//   1900 (post-立春 1900) was 庚子 (stem 6, branch 0).
void ganzhiYear(int yearForGanzhi, uint8_t& stem, uint8_t& branch) {
  const int diff = yearForGanzhi - 1900;
  stem = static_cast<uint8_t>(((diff + 6) % 10 + 10) % 10);
  branch = static_cast<uint8_t>(((diff) % 12 + 12) % 12);
}

// Month branch: 寅月 starts at 立春, 卯月 at 惊蛰, etc. So term 0 (立春) and
// term 1 (雨水) → branch 2 (寅); term 2 (惊蛰) and 3 (春分) → branch 3 (卯); ...
// In general branch = ((termIdx / 2) + 2) % 12.
//
// Month stem: 五虎遁元歌 — branch 寅 (the first month) of:
//   yearStem 甲/己 → 丙 (stem 2);  乙/庚 → 戊 (4);  丙/辛 → 庚 (6);
//   丁/壬 → 壬 (8);  戊/癸 → 甲 (0).
//   First stem of 寅 = (yearStem * 2 + 2) % 10.
// Subsequent months advance the stem by 1 per branch.
void ganzhiMonth(uint8_t yearStem, uint8_t monthBranch, uint8_t& stem, uint8_t& branch) {
  branch = monthBranch;
  const uint8_t firstStem = static_cast<uint8_t>((yearStem * 2u + 2u) % 10u);
  // 寅 = branch 2. So for monthBranch = 2 we want firstStem; +1 per branch step.
  const int delta = (static_cast<int>(monthBranch) + 12 - 2) % 12;
  stem = static_cast<uint8_t>((firstStem + delta) % 10);
}

// Day stem/branch — pure absDay arithmetic.
//   Anchor: 1984-02-02 (Gregorian) = 甲子 day = stem 0, branch 0.
//   1984-02-02 absDay = gregorianToAbsDay(1984, 2, 2).
//
// We compute this anchor lazily once via a constexpr-friendly helper.
constexpr int32_t kAnchor1984Feb02 = []() {
  int32_t days = 0;
  for (int y = 1900; y < 1984; ++y) {
    bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    days += leap ? 366 : 365;
  }
  days += 31;  // Jan has 31 days; we're in Feb 02 → 1 day into Feb
  days += 1;   // d-1 where d=2
  return days;
}();

void ganzhiDay(int32_t absDay, uint8_t& stem, uint8_t& branch) {
  int32_t diff = absDay - kAnchor1984Feb02;
  int32_t cyc = ((diff % 60) + 60) % 60;
  stem = static_cast<uint8_t>(cyc % 10);
  branch = static_cast<uint8_t>(cyc % 12);
}

}  // namespace

// =====================================================================
//  Public entry point
// =====================================================================

bool computeAlmanac(const struct tm& t, AlmanacDay& out) {
  const int gy = t.tm_year + 1900;
  const int gm = t.tm_mon + 1;
  const int gd = t.tm_mday;
  if (gy < 1900 || gy > 2100) return false;
  if (gm < 1 || gm > 12) return false;
  if (gd < 1 || gd > 31) return false;

  // Treat 2100-12-31 as the last valid day. (1900-01-01 is absDay 0.)
  const int32_t absDay = gregorianToAbsDay(gy, gm, gd);
  if (absDay < 0) return false;

  out.gregYear = static_cast<uint16_t>(gy);
  out.gregMonth = static_cast<uint8_t>(gm);
  out.gregDay = static_cast<uint8_t>(gd);
  out.weekdayIdx = weekdayFromAbsDay(absDay);

  // Lunar.
  const LunarYMD l = lunarFromAbsDay(absDay);
  out.lunarYear = l.year;
  out.lunarMonth = l.month;
  out.lunarDay = l.day;
  out.lunarLeap = l.leap;

  // ---- Current and next solar terms ---------------------------------
  // Candidate terms whose abs day is <= absDay: scan candidates in
  // calendar year `gy` and `gy-1`. Find max.
  int bestTerm = -1;
  int bestYear = gy;
  int32_t bestAbs = -1;
  for (int delta = 0; delta < 2; ++delta) {
    const int y = gy - delta;
    if (y < 1900) continue;
    for (int i = 0; i < 24; ++i) {
      const int32_t ta = termAbsDay(y, i);
      if (ta <= absDay && ta > bestAbs) {
        bestAbs = ta;
        bestTerm = i;
        bestYear = y;
      }
    }
  }
  // If we somehow fell before 1900 立春 (e.g. Jan 1900), use 0/0 as a safe
  // sentinel: caller will see daysToNextTerm > 0.
  if (bestTerm < 0) {
    bestTerm = 23;  // 大寒 of previous year
    bestYear = gy - 1;
    bestAbs = (gy - 1 >= 1900) ? termAbsDay(gy - 1, 23) : absDay;
  }
  out.termCurrentIdx = static_cast<uint8_t>(bestTerm);

  // Next term: index (bestTerm + 1) % 24, in the appropriate calendar year.
  int nextIdx = (bestTerm + 1) % 24;
  int nextYear = bestYear;
  if (nextIdx == 0) {
    // 大寒 → 立春: 大寒 sits in Jan year y, 立春 sits in Feb year y. Same year.
  } else if (nextIdx == 22) {
    // 冬至 (21, Dec) → 小寒 (22, Jan year+1).
    nextYear += 1;
  }
  if (nextYear > 2100) nextYear = 2100;
  const int32_t nextAbs = termAbsDay(nextYear, nextIdx);
  out.termNextIdx = static_cast<uint8_t>(nextIdx);
  int32_t dtn = nextAbs - absDay;
  if (dtn < 1) dtn = 1;
  if (dtn > 30) dtn = 30;
  out.daysToNextTerm = static_cast<uint8_t>(dtn);

  // ---- Ganzhi -------------------------------------------------------
  // Year ganzhi: switches at 立春. The almanac convention: any day from
  // 立春 onward belongs to the new year's ganzhi; days before 立春 of `gy`
  // still belong to the previous year's ganzhi.
  int yearForGanzhi = gy;
  if (gy >= 1900) {
    const int32_t liChunGy = termAbsDay(gy, 0);  // 立春 of calendar year gy
    if (absDay < liChunGy) {
      yearForGanzhi = gy - 1;
    }
  }
  ganzhiYear(yearForGanzhi, out.yearStemIdx, out.yearBranchIdx);

  // Month ganzhi: branch from bestTerm/2 ; 寅月 = branch 2.
  //   Terms 22/23 (小寒/大寒) → branch 1 (丑).
  //   Terms 0/1 (立春/雨水)   → branch 2 (寅).
  const uint8_t monthBranch = static_cast<uint8_t>(((bestTerm / 2) + 2) % 12);
  ganzhiMonth(out.yearStemIdx, monthBranch, out.monthStemIdx, out.monthBranchIdx);

  // Day ganzhi.
  ganzhiDay(absDay, out.dayStemIdx, out.dayBranchIdx);

  // Yi/ji indexed by day branch.
  out.yiIdx = out.dayBranchIdx;
  out.jiIdx = out.dayBranchIdx;

  // 冲: opposite branch (6 apart).
  out.clashBranchIdx = static_cast<uint8_t>((out.dayBranchIdx + 6u) % 12u);
  return true;
}

#else  // !ENABLE_CHINESE_VERSION

// Non-CN builds: stub out the API so non-CN TUs that conditionally include
// this header still link. (Currently nothing outside CN includes it.)
bool computeAlmanac(const struct tm&, AlmanacDay& out) {
  (void)out;
  return false;
}

#endif  // ENABLE_CHINESE_VERSION
