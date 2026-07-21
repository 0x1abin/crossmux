#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Plain-old-data row types shared between WeReadClient response parsing and the
// individual list Activities. Kept POD (no virtuals, no maps) so the per-row
// memory footprint stays bounded and predictable on 380 KB of DRAM.

namespace WeReadModels {

constexpr uint8_t kBookKindEbook = 0;
constexpr uint8_t kBookKindAudio = 1;
constexpr uint8_t kBookKindMp = 2;

struct BookCard {
  std::string bookId;
  std::string title;
  std::string author;
  std::string category;
  uint32_t readUpdateTime = 0;
  uint8_t finishReading = 0;
  uint8_t isTop = 0;
  uint8_t secret = 0;
  uint8_t isAlbum = kBookKindEbook;  // Row kind: e-book, audio album, or mp article collection.
};

struct NotebookRow {
  std::string bookId;
  std::string title;
  std::string author;
  int reviewCount = 0;    // ideas/comments (incl. highlight thoughts)
  int noteCount = 0;      // highlights (≈ markText entries)
  int bookmarkCount = 0;  // bookmarks (count only — content not exportable)
  int readingProgress = 0;
  uint8_t markedStatus = 0;
  uint32_t sort = 0;  // cursor for pagination via lastSort
};

struct BookmarkRow {
  std::string bookmarkId;
  std::string markText;
  std::string range;  // "start-end" range returned by the gateway.
  uint32_t chapterUid = 0;
  uint32_t createTime = 0;
};

struct ReviewMineRow {
  std::string reviewId;
  std::string content;
  std::string chapterName;  // empty for whole-book review
  int star = -1;            // -1 = no rating
  uint32_t createTime = 0;
  uint8_t isFinish = 0;
};

struct BestMarkRow {
  std::string bookmarkId;
  std::string markText;
  std::string range;
  uint32_t chapterUid = 0;
  int totalCount = 0;  // 划线人数
};

struct ChapterRow {
  std::string title;
  uint32_t chapterUid = 0;
  uint32_t chapterIdx = 0;
  int wordCount = 0;
  uint8_t level = 1;
  uint8_t paid = 1;  // 1 = free or already bought
};

struct PublicReviewRow {
  std::string reviewId;
  std::string content;
  std::string authorName;
  std::string chapterName;
  int starPercent = 0;  // raw scale: 20/40/60/80/100; 0 = no rating
  uint8_t isFinish = 0;
  uint32_t createTime = 0;
  int idx = 0;  // for pagination
};

struct SearchRow {
  std::string bookId;
  std::string title;
  std::string author;
  std::string category;
  int newRating = 0;  // 0..100 (display as /10)
  int newRatingCount = 0;
  int readingCount = 0;
  uint8_t soldout = 0;
  int searchIdx = 0;  // for maxIdx pagination
};

struct RecommendRow {
  std::string bookId;
  std::string title;
  std::string author;
  std::string reason;
  std::string category;
  int newRating = 0;
  int readingCount = 0;
  int searchIdx = 0;
};

struct StatsTopBook {
  std::string title;
  std::string author;
  uint32_t readTime = 0;  // seconds
};

struct StatsSummary {
  // seconds; conversions to "Xh Ym" happen at render time
  uint32_t totalReadTime = 0;
  uint32_t dayAverageReadTime = 0;
  int readDays = 0;
  int compareTenths = 0;  // signed integer percent * 10 (e.g. 207 = +20.7%)
  bool compareValid = false;
  std::vector<StatsTopBook> topBooks;
  std::vector<std::string> readStat;  // ["读过 12本", "笔记 45条", ...]
  std::string preferTimeWord;
  std::string preferCategoryWord;
};

}  // namespace WeReadModels
