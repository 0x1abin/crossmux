#include "WeReadBookCacheFlow.h"

#include <I18n.h>

#include <vector>

#include "WeReadCacheStore.h"
#include "WeReadModels.h"

namespace WeReadBookCacheFlow {

const char* stepName(int step) {
  switch (static_cast<Step>(step)) {
    case Step::Notes:
      return tr(STR_WEREAD_CACHE_STEP_NOTES);
    case Step::ReviewsPublic:
      return tr(STR_WEREAD_CACHE_STEP_REVIEWS_PUBLIC);
    case Step::Chapters:
      return tr(STR_WEREAD_CACHE_STEP_CHAPTERS);
    case Step::BestMarks:
      return tr(STR_WEREAD_CACHE_STEP_BESTMARKS);
    case Step::Similar:
      return tr(STR_WEREAD_CACHE_STEP_SIMILAR);
  }
  return "";
}

const char* stepApiName(int step) {
  switch (static_cast<Step>(step)) {
    case Step::Notes:
      return "/book/bookmarklist";
    case Step::ReviewsPublic:
      return "/review/list";
    case Step::Chapters:
      return "/book/chapterinfo";
    case Step::BestMarks:
      return "/book/bestbookmarks";
    case Step::Similar:
      return "/book/similar";
  }
  return "";
}

void buildRequest(int step, const std::string& bookId, JsonDocument& body, JsonDocument& filter) {
  // Filters mirror the production list Activities (WeReadNotesActivity etc.)
  // so we collect exactly the fields each reader needs when browsing offline.
  // Any drift here = blank rows on read-back.
  switch (static_cast<Step>(step)) {
    case Step::Notes:
      body["bookId"] = bookId;
      filter["updated"][0]["bookmarkId"] = true;
      filter["updated"][0]["markText"] = true;
      filter["updated"][0]["range"] = true;
      filter["updated"][0]["chapterUid"] = true;
      filter["updated"][0]["createTime"] = true;
      break;
    case Step::ReviewsPublic:
      body["bookId"] = bookId;
      body["reviewListType"] = 1;  // "推荐" — the only filter that returns rows
      // count=5 keeps the response ~30 KB; at 20 it can hit 120+ KB and
      // routinely OOMs the JSON parser on a fragmented ESP32-C3 heap.
      body["count"] = 5;
      body["maxIdx"] = 0;
      body["synckey"] = 0;
      filter["reviews"][0]["idx"] = true;
      filter["reviews"][0]["review"]["reviewId"] = true;
      filter["reviews"][0]["review"]["review"]["content"] = true;
      filter["reviews"][0]["review"]["review"]["star"] = true;
      filter["reviews"][0]["review"]["review"]["isFinish"] = true;
      filter["reviews"][0]["review"]["review"]["createTime"] = true;
      filter["reviews"][0]["review"]["review"]["chapterName"] = true;
      filter["reviews"][0]["review"]["review"]["author"]["name"] = true;
      break;
    case Step::Chapters:
      body["bookId"] = bookId;
      filter["chapters"][0]["title"] = true;
      filter["chapters"][0]["chapterUid"] = true;
      filter["chapters"][0]["chapterIdx"] = true;
      filter["chapters"][0]["wordCount"] = true;
      filter["chapters"][0]["level"] = true;
      filter["chapters"][0]["paid"] = true;
      break;
    case Step::BestMarks:
      body["bookId"] = bookId;
      body["chapterUid"] = 0;
      filter["items"][0]["bookmarkId"] = true;
      filter["items"][0]["markText"] = true;
      filter["items"][0]["range"] = true;
      filter["items"][0]["chapterUid"] = true;
      filter["items"][0]["totalCount"] = true;
      break;
    case Step::Similar:
      body["bookId"] = bookId;
      body["count"] = 12;
      body["maxIdx"] = 0;
      filter["booksimilar"]["books"][0]["idx"] = true;
      filter["booksimilar"]["books"][0]["readingCount"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["bookId"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["title"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["author"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["category"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["newRating"] = true;
      filter["booksimilar"]["books"][0]["book"]["bookInfo"]["newRatingCount"] = true;
      break;
  }
  // Always preserve the gateway-level error/upgrade fields through the filter
  // — matches WeReadFetchActivity::spawnFetch convention.
  filter["errcode"] = true;
  filter["errmsg"] = true;
  filter["upgrade_info"] = true;
}

bool parseAndSave(int step, const std::string& bookId, JsonDocument& resp) {
  switch (static_cast<Step>(step)) {
    case Step::Notes: {
      std::vector<WeReadModels::BookmarkRow> rows;
      JsonArrayConst updated = resp["updated"].as<JsonArrayConst>();
      if (!updated.isNull()) {
        rows.reserve(updated.size());
        for (JsonVariantConst b : updated) {
          WeReadModels::BookmarkRow r;
          r.bookmarkId = b["bookmarkId"] | "";
          r.markText = b["markText"] | "";
          r.range = b["range"] | "";
          r.chapterUid = b["chapterUid"] | 0u;
          r.createTime = b["createTime"] | 0u;
          if (!r.markText.empty()) rows.push_back(std::move(r));
        }
      }
      return WeReadCacheStore::saveNotes(bookId, rows);
    }
    case Step::ReviewsPublic: {
      std::vector<WeReadModels::PublicReviewRow> rows;
      JsonArrayConst reviews = resp["reviews"].as<JsonArrayConst>();
      if (!reviews.isNull()) {
        rows.reserve(reviews.size());
        for (JsonVariantConst r : reviews) {
          WeReadModels::PublicReviewRow row;
          JsonVariantConst outer = r["review"];
          JsonVariantConst inner = outer["review"];
          row.reviewId = outer["reviewId"] | "";
          row.content = inner["content"] | "";
          row.starPercent = static_cast<int>(inner["star"] | 0.0);
          row.createTime = static_cast<uint32_t>(inner["createTime"] | 0.0);
          row.isFinish = static_cast<uint8_t>(inner["isFinish"] | 0.0);
          row.chapterName = inner["chapterName"] | "";
          row.authorName = inner["author"]["name"] | "";
          row.idx = static_cast<int>(r["idx"] | 0.0);
          if (!row.content.empty() || !row.authorName.empty()) rows.push_back(std::move(row));
        }
      }
      return WeReadCacheStore::savePublicReviews(bookId, rows);
    }
    case Step::Chapters: {
      std::vector<WeReadModels::ChapterRow> rows;
      JsonArrayConst chapters = resp["chapters"].as<JsonArrayConst>();
      if (!chapters.isNull()) {
        rows.reserve(chapters.size());
        for (JsonVariantConst c : chapters) {
          WeReadModels::ChapterRow r;
          r.title = c["title"] | "";
          r.chapterUid = c["chapterUid"] | 0u;
          r.chapterIdx = c["chapterIdx"] | 0u;
          r.wordCount = c["wordCount"] | 0;
          r.level = c["level"] | 1;
          r.paid = c["paid"] | 1;
          if (!r.title.empty()) rows.push_back(std::move(r));
        }
      }
      return WeReadCacheStore::saveChapters(bookId, rows);
    }
    case Step::BestMarks: {
      std::vector<WeReadModels::BestMarkRow> rows;
      JsonArrayConst items = resp["items"].as<JsonArrayConst>();
      if (!items.isNull()) {
        rows.reserve(items.size());
        for (JsonVariantConst it : items) {
          WeReadModels::BestMarkRow r;
          r.bookmarkId = it["bookmarkId"] | "";
          r.markText = it["markText"] | "";
          r.range = it["range"] | "";
          r.chapterUid = it["chapterUid"] | 0u;
          r.totalCount = it["totalCount"] | 0;
          if (!r.markText.empty()) rows.push_back(std::move(r));
        }
      }
      return WeReadCacheStore::saveBestMarks(bookId, rows);
    }
    case Step::Similar: {
      std::vector<WeReadModels::SearchRow> rows;
      JsonArrayConst books = resp["booksimilar"]["books"].as<JsonArrayConst>();
      if (!books.isNull()) {
        rows.reserve(books.size());
        for (JsonVariantConst b : books) {
          JsonVariantConst info = b["book"]["bookInfo"];
          WeReadModels::SearchRow row;
          row.bookId = info["bookId"] | "";
          row.title = info["title"] | "";
          row.author = info["author"] | "";
          row.category = info["category"] | "";
          row.newRating = info["newRating"] | 0;
          row.newRatingCount = info["newRatingCount"] | 0;
          row.readingCount = b["readingCount"] | 0;
          row.searchIdx = b["idx"] | 0;
          if (!row.bookId.empty()) rows.push_back(std::move(row));
        }
      }
      return WeReadCacheStore::saveSimilar(bookId, rows);
    }
  }
  return false;
}

void saveEmpty(int step, const std::string& bookId) {
  switch (static_cast<Step>(step)) {
    case Step::Notes:
      WeReadCacheStore::saveNotes(bookId, {});
      return;
    case Step::ReviewsPublic:
      WeReadCacheStore::savePublicReviews(bookId, {});
      return;
    case Step::Chapters:
      WeReadCacheStore::saveChapters(bookId, {});
      return;
    case Step::BestMarks:
      WeReadCacheStore::saveBestMarks(bookId, {});
      return;
    case Step::Similar:
      WeReadCacheStore::saveSimilar(bookId, {});
      return;
  }
}

bool isFatalErr(WeReadClient::Err err) {
  return err == WeReadClient::Err::NoWifi || err == WeReadClient::Err::NoApiKey || err == WeReadClient::Err::Upgrade;
}

}  // namespace WeReadBookCacheFlow
