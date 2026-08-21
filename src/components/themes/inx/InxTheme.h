#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace InxMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics metrics = LyraMetrics::values;
  metrics.topPadding = 0;
  metrics.batteryBarHeight = 24;
  metrics.headerHeight = 66;
  metrics.verticalSpacing = 0;
  metrics.contentSidePadding = 20;
  metrics.listRowHeight = 66;
  metrics.listWithSubtitleRowHeight = 66;
  metrics.listRowGap = 0;
  metrics.listRowRadius = 0;
  metrics.listInset = 0;
  metrics.listSidePadding = 20;
  metrics.listSelectionStyle = 0;
  metrics.listScrollWidth = 6;
  metrics.listScrollSide = 0;
  metrics.listTitleBold = false;
  metrics.listSeparatorStyle = 2;
  metrics.listValueMaxWidth = 200;
  metrics.listSelectionCoversScrollReservation = true;
  metrics.menuRowHeight = 66;
  metrics.menuSpacing = 0;
  metrics.scrollBarWidth = 6;
  metrics.scrollBarRightOffset = 2;
  return metrics;
}
inline constexpr ThemeMetrics values = makeValues();
}  // namespace InxMetrics

class InxTheme final : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  bool tabIndexFromPoint(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, int x, int y,
                         int& index) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                bool showSelection = true, const std::function<bool(int index)>& rowHeading = nullptr) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon, int rowSpacing = -1) const override;
  void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                       int selectedIndex) const override;
  void drawMainTabBar(const GfxRenderer& renderer, Rect rect, MainTab selected) const override;
};
