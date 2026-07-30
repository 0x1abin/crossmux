#pragma once

#include <cstdint>
#include <cstring>
class HalFile;
class SokobanBoard {
 public:
  static constexpr int MAX_ROWS = 20;
  static constexpr int MAX_COLS = 40;

  SokobanBoard();

  enum Cell : uint8_t { EMPTY = 0, WALL, FLOOR, TARGET, BOX, BOX_ON_TARGET, PLAYER, PLAYER_ON_TARGET };

  Cell cells[MAX_ROWS][MAX_COLS] = {};
  int rows = 0;
  int cols = 0;
  int playerR = 0;
  int playerC = 0;
  int pushes = 0;

  void clear();
  bool loadFromStrings(const char* const* levelData, int numRows);
  bool movePlayer(int dr, int dc);
  bool isWin() const;
  bool loadFromFile(HalFile& f);

  void undo();
  bool canUndo() const;

 private:
  static constexpr int HISTORY_MAX = 10;
  struct HistoryEntry {
    Cell cells[MAX_ROWS][MAX_COLS];
    int playerR;
    int playerC;
    int pushes;
  };
  HistoryEntry history[HISTORY_MAX];
  int historyHead = 0;
  int historyCount = 0;

  void pushHistory();
  bool popHistory();
};