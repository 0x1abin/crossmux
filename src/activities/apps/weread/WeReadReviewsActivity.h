#pragma once

#include <string>
#include <vector>

#include "WeReadFetchActivity.h"
#include "WeReadModels.h"

class WeReadReviewsActivity final : public WeReadFetchActivity {
 public:
  WeReadReviewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookId,
                        std::string bookTitle);
  ~WeReadReviewsActivity() override = default;

 protected:
  const char* apiName() const override { return "/review/list"; }
  void buildRequest(JsonDocument& body) override;
  void buildResponseFilter(JsonDocument& filter) override;
  void parseResponse(JsonDocument& resp) override;
  int itemCount() const override { return static_cast<int>(rows_.size()); }
  const char* headerTitle() const override { return bookTitle_.c_str(); }
  void renderContent(Rect contentRect) override;
  void onConfirm(int index) override;
  void onBack() override;

 private:
  // Per-row layout cache. Recomputed when the content rect width changes.
  struct CardLayout {
    std::vector<std::string> contentLines;  // wrapped to fit, capped to kMaxLines
    std::string ratingText;                 // "N/5" badge; empty if no rating
    std::string authorText;                 // bottom-left, 10pt
    std::string dateText;                   // bottom-right, 10pt; empty if no timestamp
    int height = 0;                         // pixels
  };

  std::string bookId_;
  std::string bookTitle_;
  std::vector<WeReadModels::PublicReviewRow> rows_;

  std::vector<CardLayout> cards_;
  std::vector<int> pageStarts_;  // first-card-on-page indices; size = page count
  int layoutForWidth_ = 0;
  int layoutForHeight_ = 0;

  void ensureLayout(int contentWidth, int contentHeight);
  void rebuildPages(int contentHeight);
  int currentPageIndex() const;
};
