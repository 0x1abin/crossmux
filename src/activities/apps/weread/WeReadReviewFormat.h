#pragma once

#include <cstdint>
#include <string>

// Small formatting helpers shared by WeReadReviews list/detail activities.
// Kept here (rather than each .cpp) so the rules stay in one place — when the
// rating notation, anonymous-reader label, or date format changes, everything
// derived from a review row stays consistent.
namespace WeReadReviewFormat {

// "N/5" rating badge for a starPercent in {20,40,60,80,100}. Returns empty
// string for unrated reviews (starPercent==0). We use a numeric badge rather
// than "★" U+2605 because the Latin Noto subset shipped in the default build
// doesn't carry that glyph.
std::string rating(int starPercent);

// "YYYY-MM-DD" (UTC) for a Unix-seconds timestamp. Returns empty for ts==0.
std::string date(uint32_t unixSeconds);

// Author display name. Falls back to STR_WEREAD_ANONYMOUS_READER when empty.
std::string author(const std::string& rawAuthorName);

}  // namespace WeReadReviewFormat
