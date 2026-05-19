#pragma once

#include <string>
#include <vector>

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"

// Full-text viewer for a single public review. Mirrors WeReadHighlightDetail's
// "wrap + scroll + scrollbar" pattern; the caller pre-builds the footer string
// (rating + author + date) so this class stays unaware of WeRead row schema.
class WeReadReviewDetailActivity final : public Activity {
 public:
  WeReadReviewDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookTitle,
                             std::string content, std::string footerText);
  ~WeReadReviewDetailActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string bookTitle_;
  std::string content_;
  std::string footerText_;  // e.g. "5/5 · 张三 · 2024-12-19"; empty = no footer
  std::vector<std::string> lines_;
  int scrollOffset_ = 0;
  int visibleLines_ = 0;
  ButtonNavigator buttonNavigator;

  void rewrap();
  int maxScrollOffset() const;
};
