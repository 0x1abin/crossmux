#pragma once

#include <array>
#include <vector>

#include "InxRecentLayout.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"

struct ReadingBookStats;

class InxRecentActivity final : public Activity {
  static constexpr size_t kMaxRecentBooks = 10;

  const std::vector<RecentBook>* books = nullptr;
  std::array<const ReadingBookStats*, kMaxRecentBooks> bookStats{};
  int selected = 0;
  bool sawBackPress = false;

  InxRecentLayout layout() const;
  const ReadingBookStats* statsAt(int index) const;
  int indexFromPoint(int x, int y) const;
  void openSelected();

  void drawFlow(const Rect& content) const;
  void drawGrid(const Rect& content) const;
  void drawList(const Rect& content) const;
  void drawIcons(const Rect& content) const;
  void drawCover(const Rect& content) const;

 public:
  explicit InxRecentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InxRecent", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return MainTab::Recent; }
};
