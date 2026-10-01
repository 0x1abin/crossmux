#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "components/Rect.h"

enum class MainTab : uint8_t { None, Recent, Library, Apps, Settings, Statistics };
enum class MainTabFocus : uint8_t { Tabs, Content };
enum class MainTabContentEdge : uint8_t { First, Last };

struct MainTabLayout {
  Rect tabBar;
  Rect statusBar;
  Rect content;
};

namespace MainTabs {
inline constexpr int controlGap = 6;
inline constexpr int statusBarHeight = 28;
inline constexpr int bottomBarHeight = 56;
inline constexpr std::array<MainTab, 5> values = {MainTab::Recent, MainTab::Library, MainTab::Apps, MainTab::Settings,
                                                  MainTab::Statistics};

constexpr int indexOf(const MainTab tab) {
  const auto found = std::find(values.begin(), values.end(), tab);
  return found == values.end() ? -1 : static_cast<int>(found - values.begin());
}

constexpr MainTab adjacent(const MainTab tab, const int direction) {
  const int index = indexOf(tab);
  if (index < 0) return MainTab::None;
  const int count = static_cast<int>(values.size());
  return values[(index + (direction < 0 ? count - 1 : 1)) % count];
}

struct TabBounds {
  int left;
  int right;
};

constexpr TabBounds tabBounds(const int index, const int width) {
  const int count = static_cast<int>(values.size());
  return {width * index / count + controlGap / 2, width * (index + 1) / count - controlGap / 2};
}

constexpr MainTab fromX(const int x, const int width) {
  if (x < 0 || width <= 0 || x >= width) return MainTab::None;
  for (size_t i = 0; i < values.size(); ++i) {
    const auto bounds = tabBounds(static_cast<int>(i), width);
    if (x >= bounds.left && x < bounds.right) return values[i];
  }
  return MainTab::None;
}

constexpr MainTab backTarget(const MainTab tab) { return tab == MainTab::Recent ? MainTab::None : MainTab::Recent; }

constexpr bool showsStatusBar(const bool usesMainTabs, const bool hasTouch, const bool tabsAtBottom) {
  return usesMainTabs && hasTouch && tabsAtBottom;
}

constexpr MainTabLayout layout(const Rect& safeArea, const int topPadding, const int tabHeight, const bool tabsAtBottom,
                               const int statusHeight = 0) {
  const int top = safeArea.y + topPadding;
  const int bottom = safeArea.y + safeArea.height;
  const int status = tabsAtBottom ? statusHeight : 0;
  const int gap = status > 0 ? controlGap : 0;
  const int tabTop = tabsAtBottom ? bottom - tabHeight : top;
  const int contentTop = tabsAtBottom ? top + status + gap : top + tabHeight;
  const int contentBottom = tabsAtBottom ? tabTop - gap : bottom;
  return {Rect{safeArea.x, tabTop, safeArea.width, tabHeight}, Rect{safeArea.x, top, safeArea.width, status},
          Rect{safeArea.x, contentTop, safeArea.width, std::max(0, contentBottom - contentTop)}};
}

constexpr int contentEdgeIndex(const MainTabContentEdge edge, const int count) {
  if (count <= 0) return 0;
  switch (edge) {
    case MainTabContentEdge::First:
      return 0;
    case MainTabContentEdge::Last:
      return count - 1;
  }
  return 0;
}
}  // namespace MainTabs
