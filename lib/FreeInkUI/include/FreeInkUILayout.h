#pragma once

#include "FreeInkUICore.h"

namespace freeink {
namespace ui {

enum class LayoutLengthKind : uint8_t {
  Fixed,
  Flex,
  Token,
};

struct LayoutLength {
  LayoutLengthKind kind = LayoutLengthKind::Flex;
  int16_t value = 0;
  uint8_t flex = 1;
  uint16_t token = 0;

  static LayoutLength fixed(int16_t px) {
    LayoutLength length;
    length.kind = LayoutLengthKind::Fixed;
    length.value = px;
    return length;
  }

  static LayoutLength flexible(uint8_t grow = 1, int16_t minPx = 0) {
    LayoutLength length;
    length.kind = LayoutLengthKind::Flex;
    length.value = minPx;
    length.flex = grow == 0 ? 1 : grow;
    return length;
  }

  static LayoutLength tokenized(uint16_t tokenId) {
    LayoutLength length;
    length.kind = LayoutLengthKind::Token;
    length.token = tokenId;
    return length;
  }
};

struct LayoutRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;

  bool empty() const { return width <= 0 || height <= 0; }
};

inline int16_t layoutBasis(LayoutLength length,
                           int16_t (*resolveToken)(uint16_t, void*),
                           void* user) {
  if (length.kind == LayoutLengthKind::Fixed)
    return length.value > 0 ? length.value : 0;
  if (length.kind == LayoutLengthKind::Token && resolveToken) {
    const int16_t value = resolveToken(length.token, user);
    return value > 0 ? value : 0;
  }
  return length.value > 0 ? length.value : 0;
}

inline LayoutRect layoutRectFromUi(Rect rect) {
  return LayoutRect{rect.x, rect.y, rect.width, rect.height};
}

// Shared dynamic row/column split. This is the runtime counterpart to Stack:
// callers provide slot lengths from parsed data, and receive deterministic
// rects without heap allocation or retained widget state.
template <typename LengthAt, typename Emit>
inline void layoutLinear(LayoutRect rect, Axis axis, int16_t gap, uint8_t count,
                         LengthAt&& lengthAt, Emit&& emit,
                         int16_t (*resolveToken)(uint16_t, void*) = nullptr,
                         void* user = nullptr) {
  if (count == 0 || rect.empty())
    return;
  if (gap < 0)
    gap = 0;

  int32_t fixedTotal = 0;
  uint16_t flexTotal = 0;
  int8_t lastFlex = -1;
  for (uint8_t i = 0; i < count; ++i) {
    const LayoutLength length = lengthAt(i);
    fixedTotal += layoutBasis(length, resolveToken, user);
    if (length.kind == LayoutLengthKind::Flex) {
      flexTotal = static_cast<uint16_t>(flexTotal +
                                        (length.flex == 0 ? 1 : length.flex));
      lastFlex = static_cast<int8_t>(i);
    }
  }

  const int32_t totalGap = count > 1 ? static_cast<int32_t>(count - 1) * gap : 0;
  const int32_t mainSize = axis == Axis::Row ? rect.width : rect.height;
  int32_t remaining = mainSize - fixedTotal - totalGap;
  if (remaining < 0)
    remaining = 0;

  int32_t cursor = axis == Axis::Row ? rect.x : rect.y;
  int32_t allocatedFlex = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const LayoutLength length = lengthAt(i);
    int32_t main = layoutBasis(length, resolveToken, user);
    if (length.kind == LayoutLengthKind::Flex && flexTotal > 0) {
      const uint8_t flex = length.flex == 0 ? 1 : length.flex;
      int32_t share = (remaining * flex) / flexTotal;
      if (static_cast<int8_t>(i) == lastFlex)
        share = remaining - allocatedFlex;
      allocatedFlex += share;
      main += share;
    }

    LayoutRect slot = axis == Axis::Row ? LayoutRect{cursor, rect.y, main, rect.height}
                                        : LayoutRect{rect.x, cursor, rect.width, main};
    emit(i, slot);
    cursor += main + gap;
  }
}

template <typename LengthAt, typename Emit>
inline void layoutLinear(Rect rect, Axis axis, int16_t gap, uint8_t count,
                         LengthAt&& lengthAt, Emit&& emit,
                         int16_t (*resolveToken)(uint16_t, void*) = nullptr,
                         void* user = nullptr) {
  layoutLinear(
      layoutRectFromUi(rect), axis, gap, count, lengthAt,
      [&](uint8_t i, LayoutRect slot) {
        emit(i, Rect{static_cast<int16_t>(slot.x), static_cast<int16_t>(slot.y),
                     static_cast<int16_t>(slot.width), static_cast<int16_t>(slot.height)});
      },
      resolveToken, user);
}

struct LayoutNode {
  const char* id = nullptr;
  Axis axis = Axis::Column;
  int16_t gap = 0;
  LayoutLength length = LayoutLength::flexible();
  const LayoutNode *children = nullptr;
  uint8_t childCount = 0;
};

template <typename Emit>
inline void layoutTree(const LayoutNode& node, LayoutRect rect, Emit&& emit,
                       int16_t (*resolveToken)(uint16_t, void*) = nullptr,
                       void* user = nullptr) {
  if (!node.children || node.childCount == 0) {
    if (node.id && node.id[0] != '\0')
      emit(node.id, rect);
    return;
  }

  layoutLinear(
      rect, node.axis, node.gap, node.childCount,
      [&](uint8_t i) { return node.children[i].length; },
      [&](uint8_t i, LayoutRect childRect) {
        layoutTree(node.children[i], childRect, emit, resolveToken, user);
      },
      resolveToken, user);
}

template <typename Emit>
inline void layoutTree(const LayoutNode& node, Rect rect, Emit&& emit,
                       int16_t (*resolveToken)(uint16_t, void*) = nullptr,
                       void* user = nullptr) {
  layoutTree(
      node, layoutRectFromUi(rect),
      [&](const char* id, LayoutRect slot) {
        emit(id, Rect{static_cast<int16_t>(slot.x), static_cast<int16_t>(slot.y),
                      static_cast<int16_t>(slot.width), static_cast<int16_t>(slot.height)});
      },
      resolveToken, user);
}

}  // namespace ui
}  // namespace freeink
