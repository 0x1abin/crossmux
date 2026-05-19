#include "Game2048Board.h"

#include <esp_random.h>

#include <cstring>

void Game2048Board::reset() {
  std::memset(cells_, 0, sizeof(cells_));
  spawn();
  spawn();
}

bool Game2048Board::slideLine(uint8_t* line, uint32_t& scoreDelta) {
  uint8_t tmp[SIZE] = {};
  int w = 0;
  // Compact: drop zeros, keep order.
  for (int i = 0; i < SIZE; ++i) {
    if (line[i] != 0) tmp[w++] = line[i];
  }
  // Merge adjacent equals once.
  uint8_t out[SIZE] = {};
  int o = 0;
  for (int i = 0; i < w;) {
    if (i + 1 < w && tmp[i] == tmp[i + 1]) {
      const uint8_t merged = static_cast<uint8_t>(tmp[i] + 1);
      out[o++] = merged;
      scoreDelta += (1u << merged);
      i += 2;
    } else {
      out[o++] = tmp[i++];
    }
  }
  // Detect change vs original.
  bool changed = false;
  for (int i = 0; i < SIZE; ++i) {
    if (line[i] != out[i]) {
      changed = true;
      break;
    }
  }
  if (changed) std::memcpy(line, out, SIZE);
  return changed;
}

bool Game2048Board::slide(Direction d, uint32_t& scoreDelta) {
  bool changed = false;
  uint8_t buf[SIZE];

  // Slide each line toward index 0 of `buf`, then write back with the correct orientation.
  for (int k = 0; k < SIZE; ++k) {
    // Read line in the slide direction.
    for (int i = 0; i < SIZE; ++i) {
      switch (d) {
        case Direction::Left:
          buf[i] = cells_[k][i];
          break;
        case Direction::Right:
          buf[i] = cells_[k][SIZE - 1 - i];
          break;
        case Direction::Up:
          buf[i] = cells_[i][k];
          break;
        case Direction::Down:
          buf[i] = cells_[SIZE - 1 - i][k];
          break;
      }
    }
    if (slideLine(buf, scoreDelta)) {
      changed = true;
    }
    // Write line back.
    for (int i = 0; i < SIZE; ++i) {
      switch (d) {
        case Direction::Left:
          cells_[k][i] = buf[i];
          break;
        case Direction::Right:
          cells_[k][SIZE - 1 - i] = buf[i];
          break;
        case Direction::Up:
          cells_[i][k] = buf[i];
          break;
        case Direction::Down:
          cells_[SIZE - 1 - i][k] = buf[i];
          break;
      }
    }
  }

  if (changed) spawn();
  return changed;
}

bool Game2048Board::isWon() const {
  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      if (cells_[r][c] >= WIN_EXPONENT) return true;
    }
  }
  return false;
}

bool Game2048Board::isStuck() const {
  for (int r = 0; r < SIZE; ++r) {
    for (int c = 0; c < SIZE; ++c) {
      if (cells_[r][c] == 0) return false;
      if (c + 1 < SIZE && cells_[r][c] == cells_[r][c + 1]) return false;
      if (r + 1 < SIZE && cells_[r][c] == cells_[r + 1][c]) return false;
    }
  }
  return true;
}

void Game2048Board::spawn() {
  uint8_t empties[SIZE * SIZE];
  uint8_t count = 0;
  for (uint8_t i = 0; i < SIZE * SIZE; ++i) {
    if (cells_[i / SIZE][i % SIZE] == 0) {
      empties[count++] = i;
    }
  }
  if (count == 0) return;
  const uint8_t pick = empties[esp_random() % count];
  const uint8_t value = ((esp_random() % 10) == 0) ? 2 : 1;  // 10% chance of 4, else 2
  cells_[pick / SIZE][pick % SIZE] = value;
}
