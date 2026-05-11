#pragma once

#include <HalStorage.h>

#include <cstdint>

class GomokuBoard {
 public:
  static constexpr uint8_t MAX_SIZE = 15;
  static constexpr uint8_t MAX_CELLS = MAX_SIZE * MAX_SIZE;  // 225
  static constexpr uint8_t WIN_LEN = 5;
  static constexpr uint8_t INVALID_IDX = 0xFF;

  enum class Stone : uint8_t { Empty = 0, Black = 1, White = 2 };

  uint8_t cells[MAX_CELLS] = {};        // Stone values per intersection
  uint8_t boardSize = MAX_SIZE;         // 9 or 15
  uint8_t moveHistory[MAX_CELLS] = {};  // intersection indices in placement order
  uint16_t moveCount = 0;
  Stone winner = Stone::Empty;
  uint8_t winLineStart = INVALID_IDX;  // intersection index of one end of the 5-in-a-row
  uint8_t winLineEnd = INVALID_IDX;

  void clear(uint8_t size);
  uint8_t idx(uint8_t r, uint8_t c) const { return static_cast<uint8_t>(r * boardSize + c); }
  uint8_t rowOf(uint8_t i) const { return static_cast<uint8_t>(i / boardSize); }
  uint8_t colOf(uint8_t i) const { return static_cast<uint8_t>(i % boardSize); }

  Stone at(uint8_t r, uint8_t c) const;
  bool isEmpty(uint8_t r, uint8_t c) const;
  // Whose turn it is given moveCount (Black plays on even counts).
  Stone nextTurn() const { return (moveCount % 2 == 0) ? Stone::Black : Stone::White; }
  // Returns true if the move is legal (empty intersection, no winner yet).
  // On win, sets winner / winLineStart / winLineEnd.
  bool placeStone(uint8_t r, uint8_t c);
  // Removes the most recent move; clears any winner state.
  bool undo();
  bool isFull() const { return moveCount >= boardSize * boardSize; }
  bool isOver() const { return winner != Stone::Empty || isFull(); }
  uint16_t blackCount() const;
  uint16_t whiteCount() const;

  // Binary serialization (~230 B). Format:
  //   uint8 boardSize
  //   uint8 winner
  //   uint8 winLineStart, winLineEnd
  //   uint16 moveCount
  //   uint8 moveHistory[moveCount]   (variable length)
  // The cells[] array is reconstructed from moveHistory on read.
  bool writeTo(HalFile& f) const;
  bool readFrom(HalFile& f);

 private:
  // Returns true if the placement creates a 5-in-a-row of `color` through (r,c).
  // On win, fills winLineStart / winLineEnd with the 1st and 5th stone indices.
  bool detectWin(uint8_t r, uint8_t c, Stone color);
};
