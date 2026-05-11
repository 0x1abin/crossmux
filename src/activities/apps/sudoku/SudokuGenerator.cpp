#include "SudokuGenerator.h"

#include <Logging.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

namespace {

inline bool canPlace(const uint8_t cells[81], uint8_t r, uint8_t c, uint8_t d) {
  for (uint8_t i = 0; i < 9; i++) {
    if (cells[r * 9 + i] == d) return false;
    if (cells[i * 9 + c] == d) return false;
  }
  const uint8_t br = static_cast<uint8_t>((r / 3) * 3);
  const uint8_t bc = static_cast<uint8_t>((c / 3) * 3);
  for (uint8_t rr = br; rr < br + 3; rr++) {
    for (uint8_t cc = bc; cc < bc + 3; cc++) {
      if (cells[rr * 9 + cc] == d) return false;
    }
  }
  return true;
}

inline void shuffle9(uint8_t arr[9]) {
  for (int i = 8; i > 0; i--) {
    const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
    const uint8_t t = arr[i];
    arr[i] = arr[j];
    arr[j] = t;
  }
}

inline void shuffle81(uint8_t arr[81]) {
  for (int i = 80; i > 0; i--) {
    const int j = static_cast<int>(esp_random() % static_cast<uint32_t>(i + 1));
    const uint8_t t = arr[i];
    arr[i] = arr[j];
    arr[j] = t;
  }
}

}  // namespace

int SudokuGenerator::targetEmpties(SudokuBoard::Difficulty d) {
  switch (d) {
    case SudokuBoard::Difficulty::Easy:
      return 38;
    case SudokuBoard::Difficulty::Medium:
      return 46;
    case SudokuBoard::Difficulty::Hard:
      return 52;
  }
  return 38;
}

bool SudokuGenerator::fillSolution(uint8_t cells[81]) {
  int idx = -1;
  for (int i = 0; i < 81; i++) {
    if (cells[i] == 0) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return true;

  const uint8_t r = static_cast<uint8_t>(idx / 9);
  const uint8_t c = static_cast<uint8_t>(idx % 9);

  uint8_t order[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  shuffle9(order);

  for (int k = 0; k < 9; k++) {
    const uint8_t d = order[k];
    if (!canPlace(cells, r, c, d)) continue;
    cells[idx] = d;
    if (fillSolution(cells)) return true;
    cells[idx] = 0;
  }
  return false;
}

int SudokuGenerator::countSolutions(uint8_t cells[81], int limit) {
  if (limit <= 0) return 0;
  int idx = -1;
  for (int i = 0; i < 81; i++) {
    if (cells[i] == 0) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return 1;

  const uint8_t r = static_cast<uint8_t>(idx / 9);
  const uint8_t c = static_cast<uint8_t>(idx % 9);

  int count = 0;
  for (uint8_t d = 1; d <= 9; d++) {
    if (!canPlace(cells, r, c, d)) continue;
    cells[idx] = d;
    count += countSolutions(cells, limit - count);
    cells[idx] = 0;
    if (count >= limit) break;
  }
  return count;
}

bool SudokuGenerator::generate(SudokuBoard& board, SudokuBoard::Difficulty diff) {
  board.clear();

  uint8_t solution[81] = {};
  if (!fillSolution(solution)) {
    LOG_ERR("SDK", "Generator: fillSolution failed");
    return false;
  }

  // Save full solution
  for (int i = 0; i < 81; i++) {
    board.solution[i] = solution[i];
  }

  // Start with the full grid as the puzzle, then dig holes.
  uint8_t puzzle[81];
  memcpy(puzzle, solution, 81);

  uint8_t order[81];
  for (int i = 0; i < 81; i++) order[i] = static_cast<uint8_t>(i);
  shuffle81(order);

  const int target = targetEmpties(diff);
  int empties = 0;
  int yieldCounter = 0;

  for (int k = 0; k < 81 && empties < target; k++) {
    const uint8_t i = order[k];
    const uint8_t saved = puzzle[i];
    puzzle[i] = 0;

    uint8_t test[81];
    memcpy(test, puzzle, 81);
    const int sols = countSolutions(test, 2);
    if (sols == 1) {
      empties++;
    } else {
      puzzle[i] = saved;
    }

    // Watchdog yield. countSolutions already takes most of the time per iter.
    if (++yieldCounter >= 5) {
      yieldCounter = 0;
      vTaskDelay(1);
    }
  }

  // Commit to fixed[] and reset user/notes.
  for (int i = 0; i < 81; i++) {
    board.fixed[i] = puzzle[i];
  }
  board.resetUser();

  LOG_INF("SDK", "Generated puzzle: empties=%d (target=%d)", empties, target);
  return true;
}
