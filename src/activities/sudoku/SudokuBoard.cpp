#include "SudokuBoard.h"

#include <cstring>

void SudokuBoard::clear() {
  memset(fixed, 0, NUM_CELLS);
  memset(user, 0, NUM_CELLS);
  memset(notes, 0, sizeof(notes));
  memset(solution, 0, NUM_CELLS);
}

void SudokuBoard::resetUser() {
  memset(user, 0, NUM_CELLS);
  memset(notes, 0, sizeof(notes));
}

bool SudokuBoard::isFixed(uint8_t r, uint8_t c) const { return fixed[idx(r, c)] != 0; }

bool SudokuBoard::isEmpty(uint8_t r, uint8_t c) const {
  const uint8_t i = idx(r, c);
  return fixed[i] == 0 && user[i] == 0;
}

uint8_t SudokuBoard::getValue(uint8_t r, uint8_t c) const {
  const uint8_t i = idx(r, c);
  return fixed[i] != 0 ? fixed[i] : user[i];
}

bool SudokuBoard::placeDigit(uint8_t r, uint8_t c, uint8_t d) {
  const uint8_t i = idx(r, c);
  if (fixed[i] != 0) return false;
  user[i] = d;   // d may be 0 (effectively erase)
  notes[i] = 0;  // any digit-place clears notes
  return true;
}

void SudokuBoard::erase(uint8_t r, uint8_t c) {
  const uint8_t i = idx(r, c);
  if (fixed[i] != 0) return;
  user[i] = 0;
  notes[i] = 0;
}

void SudokuBoard::toggleNote(uint8_t r, uint8_t c, uint8_t d) {
  if (d < 1 || d > 9) return;
  const uint8_t i = idx(r, c);
  if (fixed[i] != 0 || user[i] != 0) return;
  notes[i] ^= static_cast<uint16_t>(1u << d);
}

bool SudokuBoard::hasNote(uint8_t r, uint8_t c, uint8_t d) const {
  if (d < 1 || d > 9) return false;
  return (notes[idx(r, c)] & static_cast<uint16_t>(1u << d)) != 0;
}

bool SudokuBoard::hasConflict(uint8_t r, uint8_t c, uint8_t d) const {
  if (d < 1 || d > 9) return false;
  for (uint8_t cc = 0; cc < SIZE; cc++) {
    if (cc == c) continue;
    if (getValue(r, cc) == d) return true;
  }
  for (uint8_t rr = 0; rr < SIZE; rr++) {
    if (rr == r) continue;
    if (getValue(rr, c) == d) return true;
  }
  const uint8_t br = (r / 3) * 3;
  const uint8_t bc = (c / 3) * 3;
  for (uint8_t rr = br; rr < br + 3; rr++) {
    for (uint8_t cc = bc; cc < bc + 3; cc++) {
      if (rr == r && cc == c) continue;
      if (getValue(rr, cc) == d) return true;
    }
  }
  return false;
}

bool SudokuBoard::isSolved() const {
  for (uint8_t i = 0; i < NUM_CELLS; i++) {
    const uint8_t v = (fixed[i] != 0) ? fixed[i] : user[i];
    if (v == 0) return false;
    if (solution[i] != 0 && v != solution[i]) return false;
  }
  return true;
}

uint8_t SudokuBoard::countRemaining(uint8_t digit) const {
  if (digit < 1 || digit > 9) return 0;
  uint8_t placed = 0;
  for (uint8_t i = 0; i < NUM_CELLS; i++) {
    const uint8_t v = (fixed[i] != 0) ? fixed[i] : user[i];
    if (v == digit) placed++;
  }
  return (placed >= 9) ? 0 : static_cast<uint8_t>(9 - placed);
}

uint8_t SudokuBoard::countFilled() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_CELLS; i++) {
    if (fixed[i] != 0 || user[i] != 0) n++;
  }
  return n;
}

bool SudokuBoard::writeTo(HalFile& f) const {
  if (f.write(fixed, NUM_CELLS) != NUM_CELLS) return false;
  if (f.write(user, NUM_CELLS) != NUM_CELLS) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(notes), sizeof(notes)) != sizeof(notes)) return false;
  if (f.write(solution, NUM_CELLS) != NUM_CELLS) return false;
  return true;
}

bool SudokuBoard::readFrom(HalFile& f) {
  if (f.read(fixed, NUM_CELLS) != NUM_CELLS) return false;
  if (f.read(user, NUM_CELLS) != NUM_CELLS) return false;
  if (f.read(reinterpret_cast<uint8_t*>(notes), sizeof(notes)) != sizeof(notes)) return false;
  if (f.read(solution, NUM_CELLS) != NUM_CELLS) return false;
  return true;
}
