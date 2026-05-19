#include "ChineseCalendarFace.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <time.h>

#include <cstdio>
#include <cstring>

#include "ChineseAlmanac.h"
#include "I18nKeys.h"
#include "SloppyAlphabets.h"
#include "fontIds.h"

// CN font-coverage rule (drives every fontId choice in this file):
//
//   8 / 10 / 12 / 14 pt  → CN bitmap built from cn_common_chars.txt (~3500
//                          chars).  Safe for any CJK glyph used in the design.
//   16 / 18 pt           → CN bitmap built from cn_i18n_chars.txt (~430 chars,
//                          i18n-only subset).  Glyphs absent from that file
//                          render as no-ops (getGlyph returns nullptr → silent
//                          skip), producing missing characters or blank cells.
//
// Therefore: every Chinese-text drawText / drawCenteredText / drawCenteredRow
// call in this file uses ≤14pt, with one exception — the lunar row uses 18pt
// for visual emphasis.  Every CJK char this face renders that isn't naturally
// in the 3500 SC pool *or* in a chinese.yaml STR_ value (i.e. wouldn't end up
// in cn_i18n_chars.txt) is listed in lib/EpdFont/scripts/cn_almanac_chars.txt
// — currently 戊庚壬癸寅卯巳酉戌廿蛰 (ganzhi + 惊蛰, ≤14pt) and 闰初七八九
// 十冬腊 (lunar row, 18pt).  When new vocabulary is added, append the new
// chars to that file and regenerate the CJK fonts.  ASCII digits at any size
// are fine because basic Latin (U+0020-007E) is always in the OTF subset.

namespace {

constexpr StrId kWeekdayStrIds[7] = {
    StrId::STR_CAL_WEEKDAY_SUN, StrId::STR_CAL_WEEKDAY_MON, StrId::STR_CAL_WEEKDAY_TUE, StrId::STR_CAL_WEEKDAY_WED,
    StrId::STR_CAL_WEEKDAY_THU, StrId::STR_CAL_WEEKDAY_FRI, StrId::STR_CAL_WEEKDAY_SAT,
};

// U+00B7 middle dot — guaranteed present via the Latin-1 OTF subset.
constexpr const char* kDot = "\xC2\xB7";

// CJK digits for 二〇二六 / 一九四九 — index by 0..9.  U+3007 is the CJK zero (〇).
constexpr const char* kCjkDigits[10] = {
    "\xE3\x80\x87", "一", "二", "三", "四", "五", "六", "七", "八", "九",
};

// Layout uses two vertical clusters:
//   Upper: year / month / hero / divider / 农历 / 生肖
//          divider Y is the anchor; 农历 + 生肖 hang off it.
//   Lower: 节气 / 宜·忌 boxes / footer
//          pinned to their own ratios so upper edits don't push them.
// Hero geometry: SloppyEngine is width-bound on "18" (template aspect 1.36
// > 1.0), so the visible digit size is governed by kHeroWidthRatio; the
// height ratio only sizes the bounding box and affects divider clearance.
constexpr float kHeroTopRatio = 0.18f;
constexpr float kHeroHeightRatio = 0.30f;
constexpr float kHeroWidthRatio = 0.65f;
constexpr float kDividerYRatio = 0.50f;
constexpr float kTermRowYRatio = 0.640f;
constexpr float kBoxTopYRatio = 0.710f;

constexpr int kLunarOffsetBelowDivider = 10;
constexpr int kZodiacOffsetBelowDivider = 64;
constexpr int kDividerStrokeWidth = 3;
constexpr int kDividerInsetRatio = 12;
constexpr int kBoxHeight = 130;
constexpr int kBoxSidePad = 24;
constexpr int kBoxGap = 16;
constexpr int kFooterHairlineFromBottom = 60;
constexpr int kFooterTextFromBottom = 45;

// --- Formatting helpers ------------------------------------------------

void formatYearCjk(uint16_t year, char* buf, size_t sz) {
  // 4-digit Gregorian year as 二〇二六 etc.
  buf[0] = '\0';
  size_t pos = 0;
  unsigned digits[4] = {
      (year / 1000u) % 10u,
      (year / 100u) % 10u,
      (year / 10u) % 10u,
      year % 10u,
  };
  for (int i = 0; i < 4; ++i) {
    const char* d = kCjkDigits[digits[i]];
    const size_t dlen = std::strlen(d);
    if (pos + dlen + 1 > sz) break;
    std::memcpy(buf + pos, d, dlen);
    pos += dlen;
  }
  buf[pos] = '\0';
}

void formatGanzhiYearLabel(const AlmanacDay& d, char* buf, size_t sz) {
  // "丙午年"
  std::snprintf(buf, sz, "%s%s%s", chinese_almanac::kStemNames[d.yearStemIdx],
                chinese_almanac::kBranchNames[d.yearBranchIdx], "年");
}

void formatLunarFull(const AlmanacDay& d, char* buf, size_t sz) {
  // "闰四月初三" or "四月初三"
  const char* leapPrefix = d.lunarLeap ? "闰" : "";
  std::snprintf(buf, sz, "%s%s%s", leapPrefix, chinese_almanac::kLunarMonthNames[(d.lunarMonth - 1) % 12],
                chinese_almanac::kLunarDayNames[(d.lunarDay - 1) % 30]);
}

void formatDayPillar(const AlmanacDay& d, char* buf, size_t sz) {
  std::snprintf(buf, sz, "%s%s", chinese_almanac::kStemNames[d.dayStemIdx],
                chinese_almanac::kBranchNames[d.dayBranchIdx]);
}

void formatClash(const AlmanacDay& d, char* buf, size_t sz) {
  // "未羊"  (rendered with "冲" prefix at the call site)
  std::snprintf(buf, sz, "%s%s", chinese_almanac::kBranchNames[d.clashBranchIdx],
                chinese_almanac::kZodiacNames[d.clashBranchIdx]);
}

// Draw a row of inline tokens (same font) centred horizontally in the viewport.
// `gapPx` is the literal pixel spacer between adjacent tokens.
void drawCenteredRow(GfxRenderer& renderer, int fontId, const Rect& viewport, int y, const char* const* tokens,
                     int tokenCount, int gapPx) {
  if (tokenCount <= 0) return;
  int total = 0;
  for (int i = 0; i < tokenCount; ++i) {
    total += renderer.getTextWidth(fontId, tokens[i]);
    if (i + 1 < tokenCount) total += gapPx;
  }
  int x = viewport.x + (viewport.width - total) / 2;
  for (int i = 0; i < tokenCount; ++i) {
    renderer.drawText(fontId, x, y, tokens[i], /*black=*/true);
    x += renderer.getTextWidth(fontId, tokens[i]);
    if (i + 1 < tokenCount) x += gapPx;
  }
}

int verticalCenterY(GfxRenderer& renderer, int fontId, int bandY, int heightPx) {
  return bandY + (heightPx - renderer.getFontAscenderSize(fontId)) / 2;
}

// 宜 / 忌 card: black header strip with white label + 2×2 grid of body items.
// Header label uses 12pt because 宜 / 忌 are absent from the 16/18pt
// i18n CJK subset.
void drawYiJiBox(GfxRenderer& renderer, int x, int y, int w, int h, const char* headerLabel,
                 const char* const items[4]) {
  constexpr int kHeaderH = 36;
  constexpr int kBorderW = 1;
  constexpr int kLabelFont = NOTOSANS_12_FONT_ID;
  constexpr int kItemFont = NOTOSANS_12_FONT_ID;
  constexpr int kBodyRows = 2;
  constexpr int kBodyCols = 2;

  renderer.drawRect(x, y, w, h, kBorderW, true);
  renderer.fillRect(x, y, w, kHeaderH, /*state=*/true);
  renderer.drawLine(x, y + kHeaderH, x + w - 1, y + kHeaderH, true);

  const int labelW = renderer.getTextWidth(kLabelFont, headerLabel);
  const int labelY = verticalCenterY(renderer, kLabelFont, y, kHeaderH);
  renderer.drawText(kLabelFont, x + (w - labelW) / 2, labelY, headerLabel, /*black=*/false);

  const int bodyTop = y + kHeaderH;
  const int rowH = (h - kHeaderH) / kBodyRows;
  const int colW = w / kBodyCols;
  for (int row = 0; row < kBodyRows; ++row) {
    for (int col = 0; col < kBodyCols; ++col) {
      const char* item = items[row * kBodyCols + col];
      const int textW = renderer.getTextWidth(kItemFont, item);
      const int cellY = bodyTop + row * rowH;
      const int cx = x + col * colW + (colW - textW) / 2;
      const int cy = verticalCenterY(renderer, kItemFont, cellY, rowH);
      renderer.drawText(kItemFont, cx, cy, item, /*black=*/true);
    }
  }
}

// Renders the full almanac page within `viewport`. Caller has already
// cleared the screen.  `heroStyle`/`heroSeeds` parameterize the SloppyEngine
// rendering of the big公历日 digit.
void drawAlmanacPage(GfxRenderer& renderer, const Rect& viewport, const AlmanacDay& day, const sloppy::Style& heroStyle,
                     const sloppy::Seeds& heroSeeds) {
  const int vw = viewport.width;
  const int vh = viewport.height;
  if (vw <= 0 || vh <= 0) return;

  // Year row:  二〇二六 · 丙午年
  {
    char yearCjk[16];
    char ganzhiBuf[16];
    formatYearCjk(day.gregYear, yearCjk, sizeof(yearCjk));
    formatGanzhiYearLabel(day, ganzhiBuf, sizeof(ganzhiBuf));
    const char* row[] = {yearCjk, kDot, ganzhiBuf};
    drawCenteredRow(renderer, NOTOSANS_14_FONT_ID, viewport, viewport.y + 32, row, 3, /*gap=*/8);
  }

  // Month + weekday:  5月    星期一
  {
    char monthBuf[16];
    std::snprintf(monthBuf, sizeof(monthBuf), "%u%s", static_cast<unsigned>(day.gregMonth), tr(STR_CAL_MONTH_SUFFIX));
    const uint8_t wi = (day.weekdayIdx < 7) ? day.weekdayIdx : 0;
    const char* row[] = {monthBuf, I18n::getInstance().get(kWeekdayStrIds[wi])};
    drawCenteredRow(renderer, NOTOSANS_14_FONT_ID, viewport, viewport.y + 80, row, 2, /*gap=*/40);
  }

  // Hero day digit — SloppyEngine fits to the bounding rect; width-bound.
  {
    const int heroW = static_cast<int>(vw * kHeroWidthRatio);
    const int heroH = static_cast<int>(vh * kHeroHeightRatio);
    const int heroX = viewport.x + (vw - heroW) / 2;
    const int heroY = viewport.y + static_cast<int>(vh * kHeroTopRatio);
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(day.gregDay));
    sloppy::draw(renderer, heroStyle, heroSeeds, buf, Rect{heroX, heroY, heroW, heroH});
  }

  // Bold divider — sits on the screen's vertical midline.
  const int dividerY = viewport.y + static_cast<int>(vh * kDividerYRatio);
  const int dividerInset = vw / kDividerInsetRatio;
  renderer.drawLine(viewport.x + dividerInset, dividerY, viewport.x + vw - dividerInset, dividerY, kDividerStrokeWidth,
                    /*state=*/true);

  // Lunar:  农历   闰四月初三
  {
    char lunar[24];
    formatLunarFull(day, lunar, sizeof(lunar));
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%s   %s", tr(STR_CAL_LUNAR_PREFIX), lunar);
    renderer.drawCenteredText(NOTOSANS_18_FONT_ID, dividerY + kLunarOffsetBelowDivider, buf);
  }

  // Zodiac:  生肖 · 马
  {
    const char* zodiac = chinese_almanac::kZodiacNames[day.yearBranchIdx];
    const char* row[] = {tr(STR_CAL_ZODIAC_LABEL), kDot, zodiac};
    drawCenteredRow(renderer, SMALL_FONT_ID, viewport, dividerY + kZodiacOffsetBelowDivider, row, 3,
                    /*gap=*/6);
  }

  // Solar term:  节气 · 立夏 · 距小满 2 天
  {
    const char* termCur = chinese_almanac::kSolarTermNames[day.termCurrentIdx];
    const char* termNext = chinese_almanac::kSolarTermNames[day.termNextIdx];
    char distanceBuf[40];
    std::snprintf(distanceBuf, sizeof(distanceBuf), tr(STR_CAL_DAYS_TO_NEXT_FMT), termNext,
                  static_cast<int>(day.daysToNextTerm));
    const char* row[] = {tr(STR_CAL_TERM_LABEL), kDot, termCur, kDot, distanceBuf};
    drawCenteredRow(renderer, NOTOSANS_12_FONT_ID, viewport, viewport.y + static_cast<int>(vh * kTermRowYRatio), row, 5,
                    /*gap=*/8);
  }

  // 宜 / 忌 cards.
  {
    const int boxW = (vw - kBoxSidePad * 2 - kBoxGap) / 2;
    const int boxY = viewport.y + static_cast<int>(vh * kBoxTopYRatio);
    drawYiJiBox(renderer, viewport.x + kBoxSidePad, boxY, boxW, kBoxHeight, tr(STR_CAL_YI),
                chinese_almanac::kYiPool[day.yiIdx % 12]);
    drawYiJiBox(renderer, viewport.x + kBoxSidePad + boxW + kBoxGap, boxY, boxW, kBoxHeight, tr(STR_CAL_JI),
                chinese_almanac::kJiPool[day.jiIdx % 12]);
  }

  // Footer:  hairline + "日柱 辛卯 · 冲未羊"
  {
    const int footerLineY = viewport.y + vh - kFooterHairlineFromBottom;
    renderer.drawLine(viewport.x + 24, footerLineY, viewport.x + vw - 24, footerLineY, true);

    char dayPillar[16];
    char clashBuf[16];
    formatDayPillar(day, dayPillar, sizeof(dayPillar));
    formatClash(day, clashBuf, sizeof(clashBuf));

    char leftBuf[64];
    std::snprintf(leftBuf, sizeof(leftBuf), "%s %s %s %s%s", tr(STR_CAL_DAY_PILLAR_LABEL), dayPillar, kDot,
                  tr(STR_CAL_CLASH_LABEL), clashBuf);
    renderer.drawText(SMALL_FONT_ID, viewport.x + 24, viewport.y + vh - kFooterTextFromBottom, leftBuf, /*black=*/true);
  }
}

// =====================================================================
//  ChineseCalendarFace lifecycle helpers
// =====================================================================

// Anchor: 1970-01-01 00:00:00 UTC+8 → time_t = -8*3600 (= -28800).  But we
// only ever consume real `time(nullptr)` values from `localtime_r`, so this
// helper just returns the UTC+8 midnight that contains the given local
// struct tm.
time_t startOfDayLocal(const struct tm& local) {
  struct tm midnight = local;
  midnight.tm_hour = 0;
  midnight.tm_min = 0;
  midnight.tm_sec = 0;
  midnight.tm_isdst = 0;
  // Use mktime, which assumes the struct tm is in local time (TZ already set
  // to UTC+8 by configTime).
  return mktime(&midnight);
}

// Apply day offset to a base struct tm by going through time_t arithmetic.
// Returns the resulting struct tm in local (UTC+8) zone.
bool offsetDay(const struct tm& base, int32_t daysOffset, struct tm& out) {
  time_t midnight = startOfDayLocal(base);
  if (midnight == static_cast<time_t>(-1)) return false;
  midnight += static_cast<time_t>(daysOffset) * 86400;
  if (!localtime_r(&midnight, &out)) return false;
  return true;
}

// Pre-NTP fallback: use UTC+8 today regardless of system clock.
// localtime_r with the configTime'd TZ already handles this when synced;
// when not synced, time(nullptr) returns ~ESP epoch boot time, which is
// not meaningful as a date. We still proceed (caller decided this face is
// active), but visible dates will be wrong until NTP succeeds.
bool getTodayLocal(struct tm& out) {
  const time_t now = time(nullptr);
  if (!localtime_r(&now, &out)) return false;
  return true;
}

}  // namespace

void ChineseCalendarFace::onEnter() {
  heroStyle_ = makeUniqueNoThrow<sloppy::Style>();
  heroSeeds_ = makeUniqueNoThrow<sloppy::Seeds>();
  if (!heroStyle_ || !heroSeeds_) {
    LOG_ERR("STANDBY", "OOM allocating calendar hero state");
    return;
  }

  // Stable Geometric digits — zero jitter, max stroke.
  heroStyle_->alphabet = sloppy::AlphabetId::Geometric;
  heroStyle_->wobble = 0.0f;
  heroStyle_->strokeWidth = 7;
  heroStyle_->slantDeg = 0.0f;
  heroStyle_->digitRotateMax = 0.0f;
  heroStyle_->digitGap = 18;
  heroStyle_->oneIsPlain = false;
  sloppy::preRollSeeds(/*seed=*/1u, sloppy::getAlphabet(heroStyle_->alphabet), *heroSeeds_);

  dayOffset_ = 0;
  refreshCachedDay();
}

void ChineseCalendarFace::onExit() {
  heroSeeds_.reset();
  heroStyle_.reset();
  cacheValid_ = false;
}

bool ChineseCalendarFace::refreshCachedDay() {
  struct tm today;
  if (!getTodayLocal(today)) {
    LOG_DBG("STANDBY", "Calendar: localtime_r failed");
    cacheValid_ = false;
    return false;
  }
  cachedBaseDayKey_ = today.tm_year * 512 + today.tm_yday;  // tracks date wrap

  struct tm targetTm;
  if (!offsetDay(today, dayOffset_, targetTm)) {
    cacheValid_ = false;
    return false;
  }
  if (!computeAlmanac(targetTm, cachedDay_)) {
    LOG_DBG("STANDBY", "Calendar: out-of-range offset=%ld", static_cast<long>(dayOffset_));
    cacheValid_ = false;
    return false;
  }
  cacheValid_ = true;
  return true;
}

void ChineseCalendarFace::onPagePrev() {
  // Up → previous day.
  const int32_t prev = dayOffset_ - 1;
  const int32_t saved = dayOffset_;
  dayOffset_ = prev;
  if (!refreshCachedDay()) {
    dayOffset_ = saved;  // stay on the last valid day
    refreshCachedDay();
    LOG_DBG("STANDBY", "Calendar: hit prev boundary (1900-01-01)");
  }
}

void ChineseCalendarFace::onPageNext() {
  // Down → next day.
  const int32_t next = dayOffset_ + 1;
  const int32_t saved = dayOffset_;
  dayOffset_ = next;
  if (!refreshCachedDay()) {
    dayOffset_ = saved;
    refreshCachedDay();
    LOG_DBG("STANDBY", "Calendar: hit next boundary (2100-12-31)");
  }
}

bool ChineseCalendarFace::tick() {
  // If user is parked on today (offset 0) and the wall clock has crossed
  // midnight, recompute. Otherwise stay put.
  if (dayOffset_ != 0) return false;
  struct tm today;
  if (!getTodayLocal(today)) return false;
  const int32_t key = today.tm_year * 512 + today.tm_yday;
  if (key == cachedBaseDayKey_) return false;
  refreshCachedDay();
  return true;
}

StrId ChineseCalendarFace::titleId() const { return StrId::STR_FACE_CHINESE_CALENDAR; }

uint32_t ChineseCalendarFace::secondsUntilNextWake() const {
  // No time-dependent UI inside the page (we only repaint at day crossover).
  // Return a large value; StandbyActivity's loop is event-driven anyway.
  return 3600u;
}

void ChineseCalendarFace::render(GfxRenderer& renderer, const Rect& viewport) {
  if (!heroStyle_ || !heroSeeds_) return;
  if (!cacheValid_) {
    // Compute lazily if onEnter's first attempt failed (e.g. localtime not
    // yet ready). Best-effort: if it still fails, leave the page blank.
    if (!refreshCachedDay()) return;
  }
  drawAlmanacPage(renderer, viewport, cachedDay_, *heroStyle_, *heroSeeds_);
}
