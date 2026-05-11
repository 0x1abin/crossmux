#pragma once

#include <cstdint>

#include "GomokuBoard.h"

enum class GomokuAiLevel : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

class GomokuAI {
 public:
  // Pick a move for `aiSide` on the given board. Returns intersection index.
  // Caller must verify it is the AI's turn and the game is not over.
  // Time-bounded internally; never blocks longer than the level's budget.
  static uint8_t chooseMove(const GomokuBoard& board, GomokuBoard::Stone aiSide, GomokuAiLevel level);
};
