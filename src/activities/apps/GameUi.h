#pragma once

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

class GfxRenderer;
class MappedInputManager;

inline int gameCenterY(int boxH, int textH) { return (boxH - textH) / 2; }
inline int gameCenteredBlockY(int top, int bottom, int blockH) { return top + (bottom - top - blockH) / 2; }

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen);

bool gameGridCellFromPoint(const Rect& grid, int rows, int columns, int x, int y, int& row, int& column);
bool gameIntersectionFromPoint(int originX, int originY, int pitch, int rows, int columns, int x, int y, int& row,
                               int& column);
Rect gameTouchActionRect(int screenWidth, int screenHeight, int sidePadding, int gap, int height, int index, int count);
