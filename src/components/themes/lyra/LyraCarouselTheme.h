#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace LyraCarouselMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraMetrics::values;
  v.homeTopPadding = 34;
  v.homeCoverHeight = 600;
  v.homeCoverTileHeight = 660;
  v.homeRecentBooksCount = 3;
  return v;
}();
}  // namespace LyraCarouselMetrics

class LyraCarouselTheme final : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawHomeMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                    const std::function<std::string(int index)>& buttonLabel,
                    const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle = nullptr,
                const std::function<UIIcon(int index)>& rowIcon = nullptr,
                const std::function<std::string(int index)>& rowValue = nullptr, bool highlightValue = false,
                const std::function<bool(int index)>& rowDimmed = nullptr, bool showSelection = true,
                const std::function<bool(int index)>& rowHeading = nullptr) const override;

 private:
  mutable int lastCenterIndex_ = -1;
};
