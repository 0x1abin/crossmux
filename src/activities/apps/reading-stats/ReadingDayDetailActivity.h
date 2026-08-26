#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/ReadingStatsAnalytics.h"

class ReadingDayDetailActivity final : public UiListActivity {
 public:
  explicit ReadingDayDetailActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t dayOrdinal);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void drawChrome() override;
  void drawFooter() override;

  void refreshEntries();
  void openSelectedBook();

  uint32_t dayOrdinal = 0;
  std::vector<ReadingStatsAnalytics::DayBookEntry> entries;
  std::vector<std::string> rowValues;
  std::vector<freeink::ui::ListItem> rowItems;
  std::string dateLabel;
  std::string totalReadingText;
  std::string bookCountText;
  std::string topBookTitle;
};
