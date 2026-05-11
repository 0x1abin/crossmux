#pragma once

#include <HalStorage.h>

#include <cstdint>

class SudokuBoard {
 public:
  static constexpr uint8_t SIZE = 9;
  static constexpr uint8_t NUM_CELLS = 81;

  enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

  uint8_t fixed[NUM_CELLS] = {};     // 0=空 / 1-9=题目固定
  uint8_t user[NUM_CELLS] = {};      // 0=未填 / 1-9=用户填入
  uint16_t notes[NUM_CELLS] = {};    // 位图: bit 1..9
  uint8_t solution[NUM_CELLS] = {};  // 完整解（生成时记下，用于提示与对照）

  static uint8_t idx(uint8_t r, uint8_t c) { return r * SIZE + c; }

  void clear();

  bool isFixed(uint8_t r, uint8_t c) const;
  bool isEmpty(uint8_t r, uint8_t c) const;
  uint8_t getValue(uint8_t r, uint8_t c) const;      // fixed first, else user
  bool placeDigit(uint8_t r, uint8_t c, uint8_t d);  // returns true on write; clears notes
  void erase(uint8_t r, uint8_t c);
  void toggleNote(uint8_t r, uint8_t c, uint8_t d);
  bool hasNote(uint8_t r, uint8_t c, uint8_t d) const;

  // d on board excluding cell (r,c) itself
  bool hasConflict(uint8_t r, uint8_t c, uint8_t d) const;
  bool isSolved() const;
  uint8_t countRemaining(uint8_t digit) const;  // 1..9: how many still placeable
  uint8_t countFilled() const;                  // count of fixed+user

  // Reset only user data (kept fixed + solution intact). Used by "Restart".
  void resetUser();

  // Binary serialization (~325 B). All POD, just dump arrays.
  bool writeTo(HalFile& f) const;
  bool readFrom(HalFile& f);
};
