#include "WeReadSimilarActivity.h"

#include <I18n.h>

#include <cstdio>
#include <string>

#include "../../../components/UITheme.h"
#include "../../ActivityManager.h"
#include "WeReadCacheStore.h"

WeReadSimilarActivity::WeReadSimilarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookId,
                                             std::string bookTitle)
    : WeReadFetchActivity("WeReadSimilar", renderer, mappedInput),
      bookId_(std::move(bookId)),
      bookTitle_(std::move(bookTitle)) {}

void WeReadSimilarActivity::buildRequest(JsonDocument& body) {
  body["bookId"] = bookId_;
  body["count"] = 12;
  body["maxIdx"] = 0;
}

void WeReadSimilarActivity::buildResponseFilter(JsonDocument& filter) {
  filter["booksimilar"]["books"][0]["idx"] = true;
  filter["booksimilar"]["books"][0]["readingCount"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["bookId"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["title"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["author"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["category"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["newRating"] = true;
  filter["booksimilar"]["books"][0]["book"]["bookInfo"]["newRatingCount"] = true;
}

void WeReadSimilarActivity::parseResponse(JsonDocument& resp) {
  rows_.clear();
  // /book/similar wraps the array as booksimilar.books[] per skill doc.
  JsonArrayConst books = resp["booksimilar"]["books"].as<JsonArrayConst>();
  if (books.isNull()) return;
  rows_.reserve(books.size());
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
    if (!row.bookId.empty()) rows_.push_back(std::move(row));
  }
  WeReadCacheStore::saveSimilar(bookId_, rows_);
}

void WeReadSimilarActivity::renderContent(Rect contentRect) {
  if (rows_.empty()) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_NO_SIMILAR));
    return;
  }
  static char subtitleBuf[64];
  GUI.drawList(
      renderer, contentRect, static_cast<int>(rows_.size()), selected, [this](int i) { return rows_[i].title; },
      [this](int i) {
        const auto& r = rows_[i];
        // newRating is 0..100 → /10 with one decimal.
        if (r.newRating > 0) {
          std::snprintf(subtitleBuf, sizeof(subtitleBuf), "%s · 评分 %.1f", r.author.c_str(), r.newRating / 10.0);
        } else {
          std::snprintf(subtitleBuf, sizeof(subtitleBuf), "%s", r.author.c_str());
        }
        return std::string(subtitleBuf);
      });
}

void WeReadSimilarActivity::onConfirm(int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const auto& r = rows_[index];
  activityManager.goToWeReadBook(r.bookId, r.title);
}

void WeReadSimilarActivity::onBack() { finish(); }

bool WeReadSimilarActivity::tryLoadFromCache() { return WeReadCacheStore::loadSimilar(bookId_, rows_); }
