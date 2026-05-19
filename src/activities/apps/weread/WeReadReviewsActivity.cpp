#include "WeReadReviewsActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <string>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "WeReadReviewDetailActivity.h"
#include "WeReadReviewFormat.h"
#include "WeReadTextWrap.h"

WeReadReviewsActivity::WeReadReviewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string bookId, std::string bookTitle)
    : WeReadFetchActivity("WeReadReviews", renderer, mappedInput),
      bookId_(std::move(bookId)),
      bookTitle_(std::move(bookTitle)) {}

// /review/list quirks observed against the agent gateway (do NOT trust the
// skill doc's "optional" annotations — verified the hard way):
//   - reviewListType=0 ("全部") returns an empty reviews[] even when
//     reviewsCnt > 0; only specific filters return rows. listType=1 ("推荐")
//     gives the curated list, which is the most useful default here.
//   - count / maxIdx / synckey are required despite being marked optional;
//     omitting count yields an empty reviews[].
void WeReadReviewsActivity::buildRequest(JsonDocument& body) {
  body["bookId"] = bookId_;
  body["reviewListType"] = 1;
  body["count"] = 20;
  body["maxIdx"] = 0;
  body["synckey"] = 0;
}

// Response schema (verified empirically, matches skill doc but for the depth
// of nesting which IS load-bearing):
//   reviews[].idx
//   reviews[].review.reviewId                                <- one "review"
//   reviews[].review.review.{content,star,createTime,        <- two "review"s
//                            isFinish,chapterName,author.name}
void WeReadReviewsActivity::buildResponseFilter(JsonDocument& filter) {
  filter["reviews"][0]["idx"] = true;
  filter["reviews"][0]["review"]["reviewId"] = true;
  filter["reviews"][0]["review"]["review"]["content"] = true;
  filter["reviews"][0]["review"]["review"]["star"] = true;
  filter["reviews"][0]["review"]["review"]["isFinish"] = true;
  filter["reviews"][0]["review"]["review"]["createTime"] = true;
  filter["reviews"][0]["review"]["review"]["chapterName"] = true;
  filter["reviews"][0]["review"]["review"]["author"]["name"] = true;
  filter["reviewsCnt"] = true;  // kept for the diagnostic log below
}

// WeRead serializes numeric fields as JSON doubles (e.g. createTime as
// "1.734600517e9", star as "80.0"). ArduinoJson v7's `| 0` / `| 0u` are
// strict — they return the default whenever the variant holds a double,
// silently zeroing real values. Read every numeric field via `| 0.0` and
// cast back to the target integer type.
void WeReadReviewsActivity::parseResponse(JsonDocument& resp) {
  rows_.clear();
  JsonArrayConst reviews = resp["reviews"].as<JsonArrayConst>();
  LOG_DBG("WEREAD", "review/list size=%u (reviewsCnt=%d)",
          reviews.isNull() ? 0u : static_cast<unsigned>(reviews.size()), resp["reviewsCnt"] | -1);
  if (reviews.isNull()) return;
  rows_.reserve(reviews.size());
  for (JsonVariantConst r : reviews) {
    WeReadModels::PublicReviewRow row;
    JsonVariantConst outer = r["review"];      // wraps reviewId + inner review
    JsonVariantConst inner = outer["review"];  // holds content/star/etc.
    row.reviewId = outer["reviewId"] | "";
    row.content = inner["content"] | "";
    row.starPercent = static_cast<int>(inner["star"] | 0.0);
    row.createTime = static_cast<uint32_t>(inner["createTime"] | 0.0);
    row.isFinish = static_cast<uint8_t>(inner["isFinish"] | 0.0);
    row.chapterName = inner["chapterName"] | "";
    row.authorName = inner["author"]["name"] | "";
    row.idx = static_cast<int>(r["idx"] | 0.0);
    if (!row.content.empty() || !row.authorName.empty()) rows_.push_back(std::move(row));
  }
  // Layout is rewrapped lazily by ensureLayout() on the next render — width
  // depends on the screen rect which we don't have here.
  cards_.clear();
  pageStarts_.clear();
  layoutForWidth_ = 0;
  layoutForHeight_ = 0;
}

namespace {

constexpr int kMaxContentLines = 3;
constexpr int kCardPadV = 6;       // vertical padding inside a card
constexpr int kCardSpacingV = 4;   // gap below a card before the separator
constexpr int kCardEdgePad = 12;   // horizontal padding from card edge to text/meta
constexpr int kMetaInlineGap = 6;  // gap between author and rating in the meta strip

}  // namespace

void WeReadReviewsActivity::ensureLayout(int contentWidth, int contentHeight) {
  if (cards_.size() == rows_.size() && contentWidth == layoutForWidth_) {
    if (contentHeight != layoutForHeight_) rebuildPages(contentHeight);
    return;
  }

  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int wrapWidth = contentWidth - 2 * kCardEdgePad;

  cards_.clear();
  cards_.reserve(rows_.size());
  for (const auto& r : rows_) {
    CardLayout c;
    c.contentLines = WeReadTextWrap::wrap(renderer, UI_12_FONT_ID, r.content, wrapWidth, kMaxContentLines);
    c.ratingText = WeReadReviewFormat::rating(r.starPercent);
    c.authorText = WeReadReviewFormat::author(r.authorName);
    c.dateText = WeReadReviewFormat::date(r.createTime);
    int h = kCardPadV * 2;
    h += static_cast<int>(c.contentLines.size()) * lineH12;
    if (!c.authorText.empty() || !c.ratingText.empty() || !c.dateText.empty()) h += lineH10 + 2;
    c.height = h;
    cards_.push_back(std::move(c));
  }
  layoutForWidth_ = contentWidth;
  rebuildPages(contentHeight);
}

void WeReadReviewsActivity::rebuildPages(int contentHeight) {
  pageStarts_.clear();
  if (cards_.empty()) {
    layoutForHeight_ = contentHeight;
    return;
  }
  pageStarts_.push_back(0);
  int used = 0;
  for (size_t i = 0; i < cards_.size(); ++i) {
    const int step = cards_[i].height + kCardSpacingV;
    if (used > 0 && used + step > contentHeight) {
      pageStarts_.push_back(static_cast<int>(i));
      used = step;
    } else {
      used += step;
    }
  }
  layoutForHeight_ = contentHeight;
}

int WeReadReviewsActivity::currentPageIndex() const {
  if (pageStarts_.empty()) return 0;
  for (int p = static_cast<int>(pageStarts_.size()) - 1; p >= 0; --p) {
    if (pageStarts_[p] <= selected) return p;
  }
  return 0;
}

void WeReadReviewsActivity::renderContent(Rect contentRect) {
  if (rows_.empty()) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_NO_REVIEWS));
    return;
  }

  ensureLayout(contentRect.width, contentRect.height);

  const int pageIdx = currentPageIndex();
  const int firstCard = pageStarts_[pageIdx];
  const int afterLast = (pageIdx + 1 < static_cast<int>(pageStarts_.size())) ? pageStarts_[pageIdx + 1]
                                                                              : static_cast<int>(cards_.size());

  const int lineH12 = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int textX = contentRect.x + kCardEdgePad;
  const int rightEdge = contentRect.x + contentRect.width;

  int y = contentRect.y;
  for (int i = firstCard; i < afterLast; ++i) {
    const auto& c = cards_[i];
    const int cardTop = y;

    // Selected card: light-gray dithered background under the whole card so
    // text reads black on subtle gray. Single-buffer-safe (no extra heap).
    if (i == selected) {
      renderer.fillRectDither(contentRect.x, cardTop, contentRect.width, c.height, Color::LightGray);
    }

    int cy = cardTop + kCardPadV;
    for (const auto& line : c.contentLines) {
      renderer.drawText(UI_12_FONT_ID, textX, cy, line.c_str(), true);
      cy += lineH12;
    }
    // Meta strip: "<author>  <rating>" on the left, "<date>" on the right.
    if (!c.authorText.empty() || !c.ratingText.empty() || !c.dateText.empty()) {
      const int metaY = cardTop + c.height - kCardPadV - lineH10;
      int leftX = textX;
      if (!c.authorText.empty()) {
        renderer.drawText(UI_10_FONT_ID, leftX, metaY, c.authorText.c_str(), true);
        leftX += renderer.getTextWidth(UI_10_FONT_ID, c.authorText.c_str()) + kMetaInlineGap;
      }
      if (!c.ratingText.empty()) {
        renderer.drawText(UI_10_FONT_ID, leftX, metaY, c.ratingText.c_str(), true);
      }
      if (!c.dateText.empty()) {
        const int dateW = renderer.getTextWidth(UI_10_FONT_ID, c.dateText.c_str());
        renderer.drawText(UI_10_FONT_ID, rightEdge - kCardEdgePad - dateW, metaY, c.dateText.c_str(), true);
      }
    }

    y = cardTop + c.height;
    if (i + 1 < afterLast) {
      const int sepY = y + kCardSpacingV / 2;
      renderer.drawLine(contentRect.x + 4, sepY, rightEdge - 4, sepY, true);
      y += kCardSpacingV;
    }
  }

  // Page indicator (bottom-right of content area) if there's more than one page.
  if (pageStarts_.size() > 1) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d/%d", pageIdx + 1, static_cast<int>(pageStarts_.size()));
    const int w = renderer.getTextWidth(UI_10_FONT_ID, buf);
    renderer.drawText(UI_10_FONT_ID, rightEdge - kCardEdgePad - w, contentRect.y + contentRect.height - lineH10, buf,
                      true);
  }
}

namespace {

// Join non-empty parts with " · " (U+00B7).
std::string joinWithMiddot(std::initializer_list<std::string> parts) {
  std::string out;
  for (const auto& p : parts) {
    if (p.empty()) continue;
    if (!out.empty()) out += " \xc2\xb7 ";
    out += p;
  }
  return out;
}

}  // namespace

void WeReadReviewsActivity::onConfirm(int index) {
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const auto& r = rows_[index];
  std::string footer = joinWithMiddot({WeReadReviewFormat::rating(r.starPercent),
                                       WeReadReviewFormat::author(r.authorName),
                                       WeReadReviewFormat::date(r.createTime)});
  auto handler = [this](const ActivityResult&) { requestUpdate(); };
  startActivityForResult(
      std::make_unique<WeReadReviewDetailActivity>(renderer, mappedInput, bookTitle_, r.content, std::move(footer)),
      handler);
}

void WeReadReviewsActivity::onBack() { finish(); }
