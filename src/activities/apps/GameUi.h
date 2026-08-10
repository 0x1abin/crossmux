#pragma once

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

class GfxRenderer;
class MappedInputManager;

struct GameMenuItem {
  const char* label;
  const char* hint;
};

enum class GameMenuInputResult : uint8_t { None, SelectionChanged, Activated, Dismissed };

inline int gameCenterY(int boxH, int textH) { return (boxH - textH) / 2; }
inline int gameCenteredBlockY(int top, int bottom, int blockH) { return top + (bottom - top - blockH) / 2; }

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen);

bool gameGridCellFromPoint(const Rect& grid, int rows, int columns, int x, int y, int& row, int& column);
bool gameIntersectionFromPoint(int originX, int originY, int pitch, int rows, int columns, int x, int y, int& row,
                               int& column);
Rect gameTouchActionRect(int screenWidth, int screenHeight, int sidePadding, int gap, int height, int index, int count);
Rect gameMenuPanelRect(int screenWidth, int screenHeight, int width, int titleHeight, int rowHeight, int rowCount);
GameMenuInputResult gameHandleMenuInput(MappedInputManager& input, const Rect& panel, int titleHeight, int rowHeight,
                                        int itemCount, uint8_t& selected);
void gameDrawMenu(GfxRenderer& renderer, const Rect& panel, int titleHeight, int rowHeight, const char* title,
                  const GameMenuItem* items, int itemCount, int selected);
