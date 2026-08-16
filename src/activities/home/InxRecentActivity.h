#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "InxRecentLayout.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"

struct ReadingBookStats;

class InxRecentActivity final : public Activity {
  enum class CoverCacheState : uint8_t { Unchecked, Ready, Missing, Unavailable };

  const std::vector<RecentBook>* books = nullptr;
  std::array<const ReadingBookStats*, RecentBooksStore::MAX_RECENT_BOOKS> bookStats{};
  std::array<CoverCacheState, RecentBooksStore::MAX_RECENT_BOOKS> targetCoverStates{};
  std::array<CoverCacheState, RecentBooksStore::MAX_RECENT_BOOKS> fallbackCoverStates{};
  int selected = 0;
  int thumbnailHeight = 0;

  InxRecentLayout layout() const;
  const ReadingBookStats* statsAt(int index) const;
  int indexFromPoint(int x, int y) const;
  void openSelected();
  void setThumbnailHeight(int height);
  bool tryDrawBookCover(const std::string& path, const Rect& bounds, CoverCacheState& state);
  bool drawBookCover(int bookIndex, const Rect& bounds);
  bool prepareNextMissingCover();

  void drawFlow(const Rect& content);
  void drawGrid(const Rect& content);
  void drawList(const Rect& content);
  void drawIcons(const Rect& content);
  void drawCover(const Rect& content);

 public:
  explicit InxRecentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("InxRecent", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return MainTab::Recent; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
