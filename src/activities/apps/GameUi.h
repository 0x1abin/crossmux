#pragma once

#include <cstddef>
#include <cstdint>

inline int gameCenterY(int boxH, int textH) { return (boxH - textH) / 2; }
inline int gameCenteredBlockY(int top, int bottom, int blockH) { return top + (bottom - top - blockH) / 2; }

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen);
