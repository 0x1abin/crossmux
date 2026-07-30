#include "SokobanBoard.h"

#include <HalStorage.h>

#include <cstdio>

void SokobanBoard::clear() {
  memset(cells, 0, sizeof(cells));
  rows = 0;
  cols = 0;
  playerR = 0;
  playerC = 0;
  pushes = 0;
}

SokobanBoard::SokobanBoard() : history{} {}

bool SokobanBoard::loadFromStrings(const char* const* levelData, int numRows) {
  clear();
  if (numRows <= 0 || numRows > MAX_ROWS) {
    printf("SOK: invalid rows %d\n", numRows);
    return false;
  }
  rows = numRows;
  cols = 0;
  for (int r = 0; r < rows; ++r) {
    const char* line = levelData[r];
    int c = 0;
    while (c < MAX_COLS && line[c] != '\0') {
      char ch = line[c];
      switch (ch) {
        case '#':
          cells[r][c] = WALL;
          break;
        case ' ':
          cells[r][c] = FLOOR;
          break;
        case '.':
          cells[r][c] = TARGET;
          break;
        case '$':
          cells[r][c] = BOX;
          break;
        case '*':
          cells[r][c] = BOX_ON_TARGET;
          break;
        case '@':
          cells[r][c] = PLAYER;
          playerR = r;
          playerC = c;
          break;
        case '+':
          cells[r][c] = PLAYER_ON_TARGET;
          playerR = r;
          playerC = c;
          break;
        default:
          cells[r][c] = EMPTY;
          break;
      }
      ++c;
    }
    if (c > cols) cols = c;
  }

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (cells[r][c] == EMPTY) cells[r][c] = FLOOR;
    }
  }
  printf("SOK: loaded %dx%d, player at (%d,%d)\n", rows, cols, playerR, playerC);
  return true;
}

bool SokobanBoard::movePlayer(int dr, int dc) {
  int nr = playerR + dr;
  int nc = playerC + dc;
  if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) return false;

  Cell target = cells[nr][nc];
  if (target == WALL) return false;

  pushHistory();

  if (target == BOX || target == BOX_ON_TARGET) {
    int nnr = nr + dr;
    int nnc = nc + dc;
    if (nnr < 0 || nnr >= rows || nnc < 0 || nnc >= cols) return false;
    Cell behind = cells[nnr][nnc];
    if (behind == WALL || behind == BOX || behind == BOX_ON_TARGET) return false;

    if (behind == TARGET) {
      cells[nnr][nnc] = BOX_ON_TARGET;
    } else {
      cells[nnr][nnc] = BOX;
    }
    if (target == BOX_ON_TARGET) {
      cells[nr][nc] = TARGET;
    } else {
      cells[nr][nc] = FLOOR;
    }
    pushes++;
  }

  Cell oldPos = cells[playerR][playerC];
  if (oldPos == PLAYER_ON_TARGET) {
    cells[playerR][playerC] = TARGET;
  } else {
    cells[playerR][playerC] = FLOOR;
  }

  Cell newCell = cells[nr][nc];
  if (newCell == TARGET) {
    cells[nr][nc] = PLAYER_ON_TARGET;
  } else {
    cells[nr][nc] = PLAYER;
  }

  playerR = nr;
  playerC = nc;
  return true;
}

bool SokobanBoard::isWin() const {
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (cells[r][c] == BOX) return false;
    }
  }
  return true;
}
void SokobanBoard::pushHistory() {
  if (historyCount >= HISTORY_MAX) {
    for (int i = 0; i < HISTORY_MAX - 1; ++i) {
      memcpy(&history[i], &history[i + 1], sizeof(HistoryEntry));
    }
    historyHead = HISTORY_MAX - 1;
    historyCount = HISTORY_MAX;
  } else {
    historyHead = historyCount;
    historyCount++;
  }
  HistoryEntry& entry = history[historyHead];
  memcpy(entry.cells, cells, sizeof(cells));
  entry.playerR = playerR;
  entry.playerC = playerC;
  entry.pushes = pushes;
}

bool SokobanBoard::popHistory() {
  if (historyCount == 0) return false;
  historyCount--;
  const HistoryEntry& entry = history[historyCount];
  memcpy(cells, entry.cells, sizeof(cells));
  playerR = entry.playerR;
  playerC = entry.playerC;
  pushes = entry.pushes;
  return true;
}

void SokobanBoard::undo() { popHistory(); }

bool SokobanBoard::canUndo() const { return historyCount > 0; }

bool SokobanBoard::loadFromFile(HalFile& f) {
  clear();
  uint8_t h, w;
  if (f.read(&h, 1) != 1 || f.read(&w, 1) != 1) return false;
  rows = h;
  cols = w;
  if (rows <= 0 || rows > MAX_ROWS || cols <= 0 || cols > MAX_COLS) return false;

  uint8_t buffer[MAX_COLS];
  for (int r = 0; r < rows; ++r) {
    if (f.read(buffer, cols) != cols) return false;
    for (int c = 0; c < cols; ++c) {
      cells[r][c] = static_cast<Cell>(buffer[c]);
      if (cells[r][c] == PLAYER || cells[r][c] == PLAYER_ON_TARGET) {
        playerR = r;
        playerC = c;
      }
    }
  }
  return true;
}