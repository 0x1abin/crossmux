#pragma once

#include <cstdint>

#include "engine/game.h"

enum class GoMode : uint8_t { TwoPlayer = 0, VsAi = 1 };

// ArduGO difficulty ladder (7 calibrated rungs, engine's own strength
// labels): bit0 = human color-1, bits1-2 = handicap stones, bit3 =
// komi (13 half-points = 6.5). See engine's DIFFS encoding.
enum class GoDifficulty : uint8_t {
  Kyu20 = 0,  // human Black, 3 stones, no komi
  Kyu18 = 1,  // human Black, 2 stones, no komi
  Kyu15 = 2,  // human Black, no komi
  Kyu12 = 3,  // even game (Black komi 6.5)
  Kyu10 = 4,  // human White, engine keeps no-komi edge
  Kyu7 = 5,   // engine Black + 2 stones
  Kyu5 = 6,   // engine Black + 3 stones
  Count = 7,
};

struct GoSaveSlot {
  Game game;  // engine state (board + rules + scoring)
  GoMode mode = GoMode::TwoPlayer;
  GoDifficulty difficulty = GoDifficulty::Kyu12;  // ignored for TwoPlayer
  uint8_t cursorX = 4;
  uint8_t cursorY = 4;
  uint16_t elapsedSec = 0;
};

class GoStore {
 public:
  static bool hasInProgress();
  static bool load(GoSaveSlot& out);
  static bool save(const GoSaveSlot& in);
  static bool clear();

  // Cumulative stats: wins by side (black/white), games played.
  struct GoStats {
    uint16_t blackWins = 0;
    uint16_t whiteWins = 0;
    uint16_t startedCount = 0;
  };
  static GoStats loadStats();
  static bool saveStats(const GoStats& stats);
  static void recordStart();
  static void recordWin(uint8_t winnerColor);  // BLACK(1) / WHITE(2)
};
