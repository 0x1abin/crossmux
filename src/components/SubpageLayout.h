#pragma once

#include <algorithm>

#include "components/themes/BaseTheme.h"

namespace SubpageLayout {

inline int relatedGap(const ThemeMetrics& metrics) { return std::max(4, metrics.verticalSpacing / 2); }

inline int sectionGap(const ThemeMetrics& metrics) { return std::max(12, metrics.verticalSpacing); }

inline Rect contentRect(const Rect& safeArea, const ThemeMetrics& metrics, const bool hasSubHeader = false,
                        const int footerHeight = 0) {
  const int outerGap = std::max(0, metrics.verticalSpacing);
  const int chromeHeight =
      metrics.topPadding + metrics.headerHeight + (hasSubHeader ? metrics.tabBarHeight : 0) + outerGap;
  const int top = std::clamp(chromeHeight, 0, safeArea.height);
  const int footer = std::clamp(std::max(0, footerHeight) + outerGap, 0, safeArea.height - top);
  return Rect{safeArea.x, safeArea.y + top, safeArea.width, safeArea.height - top - footer};
}

inline Rect insetHorizontal(const Rect& rect, const int inset) {
  const int boundedInset = std::clamp(inset, 0, rect.width / 2);
  return Rect{rect.x + boundedInset, rect.y, rect.width - boundedInset * 2, rect.height};
}

inline int centeredTop(const Rect& rect, const int contentHeight) {
  return rect.y + std::max(0, (rect.height - std::max(0, contentHeight)) / 2);
}

}  // namespace SubpageLayout
