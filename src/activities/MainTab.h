#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

enum class MainTab : uint8_t { None, Recent, Library, Apps, Settings, Statistics };
enum class MainTabFocus : uint8_t { Tabs, Content };
enum class MainTabContentEdge : uint8_t { First, Last };

namespace MainTabs {
inline constexpr std::array<MainTab, 5> values = {MainTab::Recent, MainTab::Library, MainTab::Apps, MainTab::Settings,
                                                  MainTab::Statistics};

constexpr int indexOf(const MainTab tab) {
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] == tab) return static_cast<int>(i);
  }
  return -1;
}

constexpr MainTab adjacent(const MainTab tab, const int direction) {
  const int index = indexOf(tab);
  if (index < 0) return MainTab::None;
  const int count = static_cast<int>(values.size());
  return values[(index + (direction < 0 ? count - 1 : 1)) % count];
}

constexpr MainTab fromX(const int x, const int width) {
  if (x < 0 || width <= 0 || x >= width) return MainTab::None;
  const int index = x * static_cast<int>(values.size()) / width;
  return values[index];
}

constexpr MainTab backTarget(const MainTab tab) { return tab == MainTab::Recent ? MainTab::None : MainTab::Recent; }

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
