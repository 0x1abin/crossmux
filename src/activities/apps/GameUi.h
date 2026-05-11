#pragma once

#include <cstddef>
#include <cstdint>

inline int gameCenterY(int boxH, int textH) { return (boxH - textH) / 2; }

void gameFormatElapsed(uint32_t ms, char* out, size_t outLen);
