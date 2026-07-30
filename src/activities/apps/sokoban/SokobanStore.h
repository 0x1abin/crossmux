#pragma once

#include <cstdint>

struct SokobanSaveSlot {
  int currentLevel = 0;
  int moves = 0;
  bool hasBoard = false;
};

class SokobanStore {
 public:
  static bool save(const SokobanSaveSlot& slot);
  static bool load(SokobanSaveSlot& out);
  static bool clear();
  static bool saveLevel(int level, int moves);
};