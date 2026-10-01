#pragma once

#include <algorithm>
#include <cstdint>

#include "components/themes/BaseTheme.h"

enum class InxRecentLayout : uint8_t { Flow, Grid, List, Icons, Cover, Count };

namespace InxRecentGeometry {
inline constexpr int footerReservedHeight = 40;

constexpr int contentHeight(const int screenHeight, const int top, const int buttonHintsHeight) {
  return std::max(0, screenHeight - top - std::max(buttonHintsHeight, footerReservedHeight));
}

inline Rect contentRect(const Rect& safeArea, const ThemeMetrics& metrics) {
  const int top = std::clamp(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, 0, safeArea.height);
  return Rect{safeArea.x, safeArea.y + top, safeArea.width,
              contentHeight(safeArea.height, top, metrics.buttonHintsHeight)};
}

inline Rect batteryRect(const Rect& safeArea) {
#if FREEINK_DEVICE_READPICO
  constexpr int bottomOffset = 24;
#else
  constexpr int bottomOffset = 30;
#endif
  return Rect{safeArea.x + safeArea.width - 12 - 15, safeArea.y + safeArea.height - bottomOffset, 15, 12};
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

inline int indexFromPoint(const Rect& content, const int x, const int y, const int selected, const int count,
                          const InxRecentLayout layout) {
  if (count <= 0 || x < content.x || x >= content.x + content.width || y < content.y || y >= content.y + content.height)
    return -1;

  int columns = 1;
  int rows = 1;
  switch (layout) {
    case InxRecentLayout::Grid:
      columns = 2;
      rows = 2;
      break;
    case InxRecentLayout::List:
      rows = 5;
      break;
    case InxRecentLayout::Icons:
      columns = 3;
      rows = 3;
      break;
    case InxRecentLayout::Flow:
    case InxRecentLayout::Cover:
      return std::clamp(selected, 0, count - 1);
    case InxRecentLayout::Count:
      return -1;
  }

  const int column = std::min(columns - 1, (x - content.x) / std::max(1, content.width / columns));
  const int row = std::min(rows - 1, (y - content.y) / std::max(1, content.height / rows));
  const int index = pageStart(selected, count, layout) + row * columns + column;
  return index < count ? index : -1;
}
}  // namespace InxRecentGeometry
