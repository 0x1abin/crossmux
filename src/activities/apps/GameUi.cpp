#include "GameUi.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen) {
  const uint32_t totalSec = ms / 1000;
  const uint32_t mm = (totalSec / 60) % 100;
  const uint32_t ss = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}

bool gameGridCellFromPoint(const Rect& grid, const int rows, const int columns, const int x, const int y, int& row,
                           int& column) {
  if (rows <= 0 || columns <= 0 || grid.width <= 0 || grid.height <= 0 || x < grid.x || y < grid.y ||
      x >= grid.x + grid.width || y >= grid.y + grid.height) {
    return false;
  }
  row = std::min(rows - 1, (y - grid.y) * rows / grid.height);
  column = std::min(columns - 1, (x - grid.x) * columns / grid.width);
  return true;
}

bool gameIntersectionFromPoint(const int originX, const int originY, const int pitch, const int rows, const int columns,
                               const int x, const int y, int& row, int& column) {
  if (pitch <= 0 || rows <= 0 || columns <= 0) return false;
  const int relativeX = x - originX;
  const int relativeY = y - originY;
  column = relativeX >= 0 ? (relativeX + pitch / 2) / pitch : (relativeX - pitch / 2) / pitch;
  row = relativeY >= 0 ? (relativeY + pitch / 2) / pitch : (relativeY - pitch / 2) / pitch;
  if (row < 0 || row >= rows || column < 0 || column >= columns) return false;
  return std::abs(x - (originX + column * pitch)) <= pitch / 2 && std::abs(y - (originY + row * pitch)) <= pitch / 2;
}

Rect gameTouchActionRect(const int screenWidth, const int screenHeight, const int sidePadding, const int gap,
                         const int height, const int index, const int count) {
  if (screenWidth <= 0 || screenHeight <= 0 || height <= 0 || count <= 0 || index < 0 || index >= count) return Rect{};
  constexpr int MIN_ACTION_GAP = 6;
  const int safePadding = std::max(0, sidePadding);
  const int safeGap = count > 1 ? std::max(MIN_ACTION_GAP, gap) : 0;
  const int availableWidth = screenWidth - safePadding * 2 - safeGap * (count - 1);
  if (availableWidth < count) return Rect{};
  const int width = availableWidth / count;
  return Rect{safePadding + index * (width + safeGap), screenHeight - safePadding - height, width, height};
}
