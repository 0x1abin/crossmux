#pragma once

#include <algorithm>
#include <cstdint>

enum class InxRecentLayout : uint8_t { Flow, Grid, List, Icons, Cover, Count };

namespace InxRecentGeometry {
inline constexpr int footerReservedHeight = 40;

constexpr int contentHeight(const int screenHeight, const int top, const int buttonHintsHeight) {
  return std::max(0, screenHeight - top - std::max(buttonHintsHeight, footerReservedHeight));
}

constexpr int itemsPerPage(const InxRecentLayout layout) {
  switch (layout) {
    case InxRecentLayout::Flow:
    case InxRecentLayout::Cover:
      return 1;
    case InxRecentLayout::Grid:
      return 4;
    case InxRecentLayout::List:
      return 5;
    case InxRecentLayout::Icons:
      return 9;
    case InxRecentLayout::Count:
      return 1;
  }
  return 1;
}

constexpr int pageStart(const int selected, const int count, const InxRecentLayout layout) {
  if (count <= 0) return 0;
  const int clamped = std::clamp(selected, 0, count - 1);
  const int pageItems = itemsPerPage(layout);
  return clamped / pageItems * pageItems;
}
}  // namespace InxRecentGeometry
