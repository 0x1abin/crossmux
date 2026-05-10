#include "GomokuGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "GomokuMenuActivity.h"

namespace {

// Centralized font roles, mirroring Sudoku.
constexpr int kStatusFont = UI_12_FONT_ID;           // title bar / mode line
constexpr int kModalItemFont = UI_12_FONT_ID;        // game-menu modal rows
constexpr int kModalHintFont = UI_10_FONT_ID;        // game-menu modal right-side hints
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;      // win-screen big title
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;  // win-screen stat values + info-panel stat values
constexpr int kRecentFont = UI_12_FONT_ID;           // recent moves list

// Debounce window for SD writes triggered by gameplay; mirrors CLAUDE.md's
// SPIFFS-write-throttling guidance (avoid per-input writes to a finite-erase medium).
constexpr uint32_t kSaveDebounceMs = 1500;

// Vertically center text of height `textH` inside a box of height `boxH`.
inline int centerY(int boxH, int textH) { return (boxH - textH) / 2; }

const char* modeLabel(GomokuMode m) {
  return (m == GomokuMode::VsAi) ? tr(STR_GOMOKU_MODE_AI) : tr(STR_GOMOKU_MODE_2P);
}

}  // namespace

GomokuGameActivity::GomokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GomokuMode mode,
                                       uint8_t boardSize, bool resume)
    : Activity("Gomoku", renderer, mappedInput), mode(mode), resumeRequested(resume) {
  board.clear((boardSize == 9) ? 9 : 15);
  cursorR = static_cast<uint8_t>((board.boardSize - 1) / 2);
  cursorC = cursorR;
}

void GomokuGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  state = State::Playing;
  elapsedMs = 0;
  lastTickMs = millis();
  pendingSaveAtMs = 0;
  menuSel = 0;
  statsRecorded = false;
  resignedFlag = false;
  resignWinner = GomokuBoard::Stone::Empty;

  if (resumeRequested) {
    GomokuSaveSlot slot;
    if (GomokuStore::load(slot)) {
      board = slot.board;
      mode = slot.mode;
      cursorR = (slot.cursorR < board.boardSize) ? slot.cursorR : static_cast<uint8_t>((board.boardSize - 1) / 2);
      cursorC = (slot.cursorC < board.boardSize) ? slot.cursorC : cursorR;
      elapsedMs = static_cast<uint32_t>(slot.elapsedSec) * 1000u;
      // Resumed game already counted via recordStart on its first start; do not double count.
      // If the saved board has a winner already, jump straight to GameOver (rare but possible).
      if (board.winner != GomokuBoard::Stone::Empty || board.isFull()) {
        state = State::GameOver;
        statsRecorded = true;  // already recorded on the original session.
      }
    } else {
      LOG_ERR("GMK", "Resume save unreadable; clearing and starting fresh");
      GomokuStore::clear();
      resumeRequested = false;
      GomokuStore::recordStart(board.boardSize);
    }
  } else {
    GomokuStore::recordStart(board.boardSize);
  }

  requestUpdate();
}

void GomokuGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void GomokuGameActivity::loop() {
  if (state == State::Playing) {
    const uint32_t now = millis();
    if (now > lastTickMs) {
      elapsedMs += now - lastTickMs;
    }
    lastTickMs = now;

    if (pendingSaveAtMs != 0 && now >= pendingSaveAtMs) {
      pendingSaveAtMs = 0;
      flushSave();
    }

    handleInputPlaying();
  } else if (state == State::GameMenu) {
    handleInputGameMenu();
  } else if (state == State::GameOver) {
    handleInputGameOver();
  }
}

// ---------- Geometry ----------

int GomokuGameActivity::boardPitch() const { return (board.boardSize == 15) ? 30 : 50; }
int GomokuGameActivity::boardOriginX() const { return (board.boardSize == 15) ? 30 : 40; }
int GomokuGameActivity::boardOriginY() const { return BOARD_AREA_Y + boardPitch() / 2; }
int GomokuGameActivity::stoneRadius() const { return (board.boardSize == 15) ? 12 : 20; }

void GomokuGameActivity::intersectionXY(uint8_t r, uint8_t c, int* x, int* y) const {
  *x = boardOriginX() + static_cast<int>(c) * boardPitch();
  *y = boardOriginY() + static_cast<int>(r) * boardPitch();
}

void GomokuGameActivity::coordToText(uint8_t r, uint8_t c, char* out, size_t outLen) const {
  // Column: Go-style — skip 'I' to avoid confusion with '1' / '|'.
  // 0..7 → A..H,  8..14 → J..P (15×15);  9×9 max col 8 → J.
  // Row:  boardSize - r so the top row prints as the largest number.
  const char letter = (c < 8) ? static_cast<char>('A' + c) : static_cast<char>('A' + c + 1);
  snprintf(out, outLen, "%c%u", letter, static_cast<unsigned>(board.boardSize - r));
}

void GomokuGameActivity::formatTime(uint32_t ms, char* out, size_t outLen) const {
  const uint32_t totalSec = ms / 1000;
  const uint32_t mm = (totalSec / 60) % 100;
  const uint32_t ss = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}

// ---------- Input ----------

void GomokuGameActivity::handleInputPlaying() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveCursor(-1, 0);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveCursor(1, 0);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveCursor(0, -1);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveCursor(0, 1);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    doPlace();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    enterGameMenu();
    requestUpdate();
  }
}

void GomokuGameActivity::handleInputGameMenu() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    menuSel = static_cast<uint8_t>((menuSel + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    menuSel = static_cast<uint8_t>((menuSel + 1) % MENU_ITEM_COUNT);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    runMenuItem(menuSel);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    resumeFromMenu();
    requestUpdate();
  }
}

void GomokuGameActivity::handleInputGameOver() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto m = mode;
    const uint8_t bs = board.boardSize;
    activityManager.replaceActivity(std::make_unique<GomokuGameActivity>(renderer, mappedInput, m, bs, false));
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivity(std::make_unique<GomokuMenuActivity>(renderer, mappedInput));
  }
}

void GomokuGameActivity::moveCursor(int dr, int dc) {
  const int n = board.boardSize;
  cursorR = static_cast<uint8_t>((cursorR + dr + n) % n);
  cursorC = static_cast<uint8_t>((cursorC + dc + n) % n);
}

void GomokuGameActivity::doPlace() {
  if (board.winner != GomokuBoard::Stone::Empty) return;
  if (!board.isEmpty(cursorR, cursorC)) return;  // can't place on occupied intersection
  if (!board.placeStone(cursorR, cursorC)) return;
  scheduleSave();
  if (board.winner != GomokuBoard::Stone::Empty || board.isFull()) {
    onGameOver();
  }
}

void GomokuGameActivity::onGameOver() {
  if (state == State::GameOver) return;
  state = State::GameOver;
  if (!statsRecorded) {
    const uint32_t totalSec = elapsedMs / 1000;
    const uint16_t cap = static_cast<uint16_t>(totalSec > 0xFFFF ? 0xFFFF : totalSec);
    if (resignedFlag) {
      GomokuStore::recordWin(board.boardSize, resignWinner, cap);
    } else if (board.winner != GomokuBoard::Stone::Empty) {
      GomokuStore::recordWin(board.boardSize, board.winner, cap);
    } else {
      // Board full without a 5-in-a-row.
      GomokuStore::recordDraw(board.boardSize);
    }
    GomokuStore::clear();
    pendingSaveAtMs = 0;
    statsRecorded = true;
  }
}

void GomokuGameActivity::enterGameMenu() {
  state = State::GameMenu;
  menuSel = 0;
}

void GomokuGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();
}

void GomokuGameActivity::runMenuItem(uint8_t i) {
  switch (i) {
    case 0:  // Resume
      resumeFromMenu();
      return;
    case 1:  // Undo
      if (board.moveCount > 0) {
        board.undo();
        scheduleSave();
      }
      resumeFromMenu();
      return;
    case 2: {  // Resign — current side to move resigns; the other side wins.
      const GomokuBoard::Stone resigner = board.nextTurn();
      resignedFlag = true;
      resignWinner = (resigner == GomokuBoard::Stone::Black) ? GomokuBoard::Stone::White : GomokuBoard::Stone::Black;
      board.winner = resignWinner;  // for win-screen display
      board.winLineStart = GomokuBoard::INVALID_IDX;
      board.winLineEnd = GomokuBoard::INVALID_IDX;
      onGameOver();
      return;
    }
    case 3: {  // New Game (same mode + size)
      const auto m = mode;
      const uint8_t bs = board.boardSize;
      GomokuStore::clear();
      activityManager.replaceActivity(std::make_unique<GomokuGameActivity>(renderer, mappedInput, m, bs, false));
      return;
    }
    case 4:  // Exit to menu
      flushSave();
      activityManager.replaceActivity(std::make_unique<GomokuMenuActivity>(renderer, mappedInput));
      return;
  }
}

void GomokuGameActivity::scheduleSave() { pendingSaveAtMs = millis() + kSaveDebounceMs; }

void GomokuGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  pendingSaveAtMs = 0;
  GomokuSaveSlot slot;
  slot.board = board;
  slot.mode = mode;
  slot.aiDifficulty = 1;
  slot.cursorR = cursorR;
  slot.cursorC = cursorC;
  slot.elapsedSec = static_cast<uint16_t>(elapsedMs / 1000);
  if (!GomokuStore::save(slot)) {
    LOG_ERR("GMK", "Failed to write save");
  }
}

// ---------- Render dispatch ----------

void GomokuGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  switch (state) {
    case State::Playing:
      renderPlaying();
      break;
    case State::GameMenu:
      renderPlaying();
      renderGameMenu();
      break;
    case State::GameOver:
      renderGameOver();
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void GomokuGameActivity::renderPlaying() {
  drawTitleBar();
  drawBoard();
  drawInfoPanel();
  drawModeLine();
  drawFooter();
}

// ---------- Title bar ----------

void GomokuGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H, w, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = centerY(TITLE_BAR_H, textH);

  char left[64];
  snprintf(left, sizeof(left), "%s · %s", tr(STR_GOMOKU_TITLE), modeLabel(mode));
  renderer.drawText(kStatusFont, 12, y, left);

  // Right side: small stone dot + move count + time.
  // Dot reflects whose turn is next during play (or the winner once over).
  const GomokuBoard::Stone marker = (board.winner != GomokuBoard::Stone::Empty) ? board.winner : board.nextTurn();
  const bool markerIsBlack = (marker == GomokuBoard::Stone::Black);

  char timeStr[8];
  formatTime(elapsedMs, timeStr, sizeof(timeStr));
  char tail[24];
  snprintf(tail, sizeof(tail), "%u · %s", static_cast<unsigned>(board.moveCount), timeStr);
  const int rw = renderer.getTextWidth(kStatusFont, tail);
  constexpr int dotR = 4;
  constexpr int dotGap = 4;
  const int totalW = 2 * dotR + dotGap + rw;
  const int rx = w - 12 - totalW;
  const int dotCx = rx + dotR;
  const int dotCy = TITLE_BAR_H / 2;
  drawStone(dotCx, dotCy, dotR, markerIsBlack);
  renderer.drawText(kStatusFont, rx + 2 * dotR + dotGap, y, tail);
}

// ---------- Board ----------

void GomokuGameActivity::drawBoard() {
  const int n = board.boardSize;
  const int pitch = boardPitch();
  const int ox = boardOriginX();
  const int oy = boardOriginY();
  const int len = (n - 1) * pitch;

  // Grid lines (1px black).
  for (int i = 0; i < n; i++) {
    renderer.drawLine(ox, oy + i * pitch, ox + len, oy + i * pitch, true);
    renderer.drawLine(ox + i * pitch, oy, ox + i * pitch, oy + len, true);
  }

  // Star points (hoshi).
  static constexpr uint8_t kHoshi15[5][2] = {{3, 3}, {3, 11}, {7, 7}, {11, 3}, {11, 11}};
  static constexpr uint8_t kHoshi9[5][2] = {{2, 2}, {2, 6}, {4, 4}, {6, 2}, {6, 6}};
  const uint8_t (*hoshi)[2] = (n == 15) ? kHoshi15 : kHoshi9;
  for (int k = 0; k < 5; k++) {
    const int hx = ox + hoshi[k][1] * pitch;
    const int hy = oy + hoshi[k][0] * pitch;
    renderer.fillRoundedRect(hx - 3, hy - 3, 7, 7, 3, Color::Black);
  }

  // Stones.
  const int sr = stoneRadius();
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      const auto v = board.at(static_cast<uint8_t>(r), static_cast<uint8_t>(c));
      if (v == GomokuBoard::Stone::Empty) continue;
      const int sx = ox + c * pitch;
      const int sy = oy + r * pitch;
      drawStone(sx, sy, sr, v == GomokuBoard::Stone::Black);
    }
  }

  // Last-move marker: small reverse-color dot on top of the most recent stone.
  if (board.moveCount > 0) {
    const uint8_t lastIdx = board.moveHistory[board.moveCount - 1];
    const uint8_t lr = board.rowOf(lastIdx);
    const uint8_t lc = board.colOf(lastIdx);
    const int sx = ox + lc * pitch;
    const int sy = oy + lr * pitch;
    const auto color = board.at(lr, lc);
    const Color dotColor = (color == GomokuBoard::Stone::Black) ? Color::White : Color::Black;
    renderer.fillRoundedRect(sx - 2, sy - 2, 5, 5, 2, dotColor);
  }

  // Cursor: 24×24 hollow box centered on intersection (mirrors Sudoku palette focus).
  if (state == State::Playing && board.winner == GomokuBoard::Stone::Empty) {
    int cx, cy;
    intersectionXY(cursorR, cursorC, &cx, &cy);
    constexpr int half = 12;
    renderer.drawRect(cx - half, cy - half, 2 * half, 2 * half, 2, true);
  }
}

void GomokuGameActivity::drawStone(int cx, int cy, int radius, bool isBlack) const {
  const int side = 2 * radius + 1;
  if (isBlack) {
    renderer.fillRoundedRect(cx - radius, cy - radius, side, side, radius, Color::Black);
  } else {
    // Erase grid lines under the stone, then black outline.
    renderer.fillRoundedRect(cx - radius, cy - radius, side, side, radius, Color::White);
    renderer.drawRoundedRect(cx - radius, cy - radius, side, side, 2, radius, true);
  }
}

void GomokuGameActivity::drawWinLine() {
  if (board.winLineStart == GomokuBoard::INVALID_IDX || board.winLineEnd == GomokuBoard::INVALID_IDX) return;
  int x1, y1, x2, y2;
  intersectionXY(board.rowOf(board.winLineStart), board.colOf(board.winLineStart), &x1, &y1);
  intersectionXY(board.rowOf(board.winLineEnd), board.colOf(board.winLineEnd), &x2, &y2);
  // Triple draw for a 2-pixel-wide line that handles both axis-aligned and
  // diagonal directions reasonably (drawLine's lineWidth only thickens along Y).
  renderer.drawLine(x1, y1, x2, y2, true);
  renderer.drawLine(x1 + 1, y1, x2 + 1, y2, true);
  renderer.drawLine(x1, y1 + 1, x2, y2 + 1, true);
}

// ---------- Info panel ----------

void GomokuGameActivity::drawInfoPanel() {
  const int sw = renderer.getScreenWidth();
  const int innerW = sw - 2 * CONTENT_X;

  // Two stat cells, narrower and shorter than v3, centered horizontally.
  // Inside each: a board-sized stone icon next to the count — the colour
  // itself identifies the side, so no "Black"/"White" text label.
  constexpr int statH = 60;
  constexpr int cellW = 160;
  constexpr int cellGap = 24;
  const int statY = INFO_PANEL_Y;
  const int totalW = 2 * cellW + cellGap;
  const int statXStart = (sw - totalW) / 2;
  const int statStoneR = stoneRadius();  // matches the board (12 for 15×15, 20 for 9×9)

  // Active-turn indicator: a small filled triangle on the active side's cell.
  // Hidden when the game is over (winner set) or the board is a draw-full.
  const bool showTurn = (board.winner == GomokuBoard::Stone::Empty) && !board.isFull();
  const GomokuBoard::Stone activeSide = showTurn ? board.nextTurn() : GomokuBoard::Stone::Empty;

  auto drawStatCell = [&](int xLeft, GomokuBoard::Stone color) {
    renderer.drawRect(xLeft, statY, cellW, statH, true);
    const uint16_t count = (color == GomokuBoard::Stone::Black) ? board.blackCount() : board.whiteCount();
    char numBuf[8];
    snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(count));
    const int valH = renderer.getTextHeight(kStatValueFont);
    const int valW = renderer.getTextWidth(kStatValueFont, numBuf);
    constexpr int gap = 10;
    const int groupW = 2 * statStoneR + gap + valW;
    const int gx = xLeft + (cellW - groupW) / 2;
    const int cy = statY + statH / 2;  // group is vertically centred in the cell
    drawStone(gx + statStoneR, cy, statStoneR, color == GomokuBoard::Stone::Black);
    // NOTOSANS_16 digits have no descenders, so a pure (cy - valH/2) places
    // the optical centre a few px below the stone. Bias up 4 px to align.
    constexpr int valOpticalBias = 4;
    renderer.drawText(kStatValueFont, gx + 2 * statStoneR + gap, cy - valH / 2 - valOpticalBias, numBuf);

    // Right-pointing filled triangle ▶ on the active side.
    if (color == activeSide) {
      constexpr int triW = 8;
      constexpr int triH = 12;
      const int tx = xLeft + 10;
      const int ty = statY + (statH - triH) / 2;
      const int xs[3] = {tx, tx + triW, tx};
      const int ys[3] = {ty, ty + triH / 2, ty + triH};
      renderer.fillPolygon(xs, ys, 3, true);
    }
  };

  drawStatCell(statXStart, GomokuBoard::Stone::Black);
  drawStatCell(statXStart + cellW + cellGap, GomokuBoard::Stone::White);

  // Recent-moves strip beneath the stat cells (3 most recent, evenly spaced).
  // Step number lives in the title bar, so each slot just shows
  // [stone-icon] [coord] in a clearly readable size.
  const int recentY = statY + statH + 16;
  const int textH = renderer.getTextHeight(kRecentFont);
  const int slotW = innerW / 3;
  const int firstShown = (board.moveCount >= 3) ? (board.moveCount - 3) : 0;
  for (uint16_t i = firstShown; i < board.moveCount; i++) {
    const uint8_t cellIdx = board.moveHistory[i];
    const uint8_t r = board.rowOf(cellIdx);
    const uint8_t c = board.colOf(cellIdx);
    const bool isBlack = (i % 2 == 0);
    char coord[8];
    coordToText(r, c, coord, sizeof(coord));
    const int slotIdx = static_cast<int>(i - firstShown);
    const int sx = CONTENT_X + slotIdx * slotW;
    constexpr int dotR = 6;
    constexpr int dotGap = 6;
    const int tw = renderer.getTextWidth(kRecentFont, coord);
    const int groupW = 2 * dotR + dotGap + tw;
    const int gx = sx + (slotW - groupW) / 2;
    drawStone(gx + dotR, recentY + textH / 2, dotR, isBlack);
    renderer.drawText(kRecentFont, gx + 2 * dotR + dotGap, recentY, coord);
  }
}

// ---------- Mode line ----------

void GomokuGameActivity::drawModeLine() {
  const int y = MODE_LINE_Y;
  // Left text: "Last <coord> · Cursor <coord>"
  char lastBuf[8] = "--";
  if (board.moveCount > 0) {
    const uint8_t cellIdx = board.moveHistory[board.moveCount - 1];
    coordToText(board.rowOf(cellIdx), board.colOf(cellIdx), lastBuf, sizeof(lastBuf));
  }
  char cursorBuf[8];
  coordToText(cursorR, cursorC, cursorBuf, sizeof(cursorBuf));
  char left[64];
  snprintf(left, sizeof(left), "%s %s · %s %s", tr(STR_GOMOKU_LAST), lastBuf, tr(STR_GOMOKU_CURSOR), cursorBuf);
  renderer.drawText(kStatusFont, CONTENT_X, y, left);

  // Turn indicator was previously a right-side pill here; in v5 it moved
  // into the active stat cell as a filled triangle, so the right half of
  // the mode line is intentionally empty.
}

// ---------- Footer (button hints) ----------

void GomokuGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* upLabel = "";
  const char* downLabel = "";

  if (state == State::Playing) {
    backLabel = tr(STR_GOMOKU_MENU);
    confirmLabel = tr(STR_GOMOKU_PLACE);
    upLabel = tr(STR_DIR_UP);
    downLabel = tr(STR_DIR_DOWN);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_GOMOKU_RESUME);
    confirmLabel = tr(STR_SELECT);
    upLabel = tr(STR_DIR_UP);
    downLabel = tr(STR_DIR_DOWN);
  } else if (state == State::GameOver) {
    backLabel = tr(STR_GOMOKU_MENU);
    confirmLabel = tr(STR_GOMOKU_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, upLabel, downLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------- Game Menu modal ----------

void GomokuGameActivity::renderGameMenu() {
  // Compact modal: title 28 + 5 rows × 32 + 4 = 192 px tall, 320 px wide. Mirrors Sudoku.
  constexpr int titleH = 28;
  constexpr int rowH = 32;
  const int w = 320;
  const int h = titleH + rowH * MENU_ITEM_COUNT + 4;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = (renderer.getScreenHeight() - h) / 2;

  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, 2, true);

  const int titleTextH = renderer.getTextHeight(kModalItemFont);
  renderer.fillRect(x + 2, y + titleH, w - 4, 1, true);
  renderer.drawText(kModalItemFont, x + 12, y + centerY(titleH, titleTextH), tr(STR_GOMOKU_GAME_MENU));

  const char* labels[MENU_ITEM_COUNT] = {
      tr(STR_GOMOKU_RESUME), tr(STR_GOMOKU_UNDO), tr(STR_GOMOKU_RESIGN), tr(STR_GOMOKU_NEW_GAME), tr(STR_GOMOKU_EXIT),
  };

  // Right-side hints
  char undoHint[24] = "";
  if (board.moveCount > 0) {
    snprintf(undoHint, sizeof(undoHint), tr(STR_GOMOKU_MOVE_FMT), static_cast<unsigned>(board.moveCount));
  }
  const char* hints[MENU_ITEM_COUNT] = {"", undoHint, "", "", tr(STR_GOMOKU_HOME)};

  const int itemTextH = renderer.getTextHeight(kModalItemFont);
  const int hintTextH = renderer.getTextHeight(kModalHintFont);
  const int firstY = y + titleH;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int rowY = firstY + i * rowH;
    const bool inverted = (i == menuSel);
    if (inverted) {
      renderer.fillRect(x + 1, rowY, w - 2, rowH, true);
    }
    renderer.drawText(kModalItemFont, x + 12, rowY + centerY(rowH, itemTextH), labels[i], !inverted);
    if (hints[i] && hints[i][0] != '\0') {
      const int hw = renderer.getTextWidth(kModalHintFont, hints[i]);
      renderer.drawText(kModalHintFont, x + w - 12 - hw, rowY + centerY(rowH, hintTextH) + 2, hints[i], !inverted);
    }
  }
}

// ---------- Game Over screen ----------

void GomokuGameActivity::renderGameOver() {
  drawTitleBar();
  const int sw = renderer.getScreenWidth();

  // Determine outcome text.
  GomokuBoard::Stone winner = resignedFlag ? resignWinner : board.winner;
  const char* titleStr = nullptr;
  if (winner == GomokuBoard::Stone::Black) {
    titleStr = tr(STR_GOMOKU_BLACK_WINS);
  } else if (winner == GomokuBoard::Stone::White) {
    titleStr = tr(STR_GOMOKU_WHITE_WINS);
  } else {
    titleStr = tr(STR_GOMOKU_DRAW);
  }

  // Win line drawn on top of the (already-rendered) board only matters if we
  // also draw the board behind the title — to avoid clutter we keep the
  // title-screen layout purely textual, mirroring SudokuGameActivity::renderWon.

  const int titleY = 200;
  renderer.drawCenteredText(kHeroFont, titleY, titleStr, true, EpdFontFamily::BOLD);

  // Subtitle: mode · size · time · N moves
  char timeBuf[8];
  formatTime(elapsedMs, timeBuf, sizeof(timeBuf));
  const char* sizeLabel = (board.boardSize == 9) ? tr(STR_GOMOKU_BOARD_9) : tr(STR_GOMOKU_BOARD_15);
  char sub[96];
  snprintf(sub, sizeof(sub), "%s · %s · %s · %u %s", modeLabel(mode), sizeLabel, timeBuf,
           static_cast<unsigned>(board.moveCount), tr(STR_GOMOKU_MOVES_SUFFIX));
  renderer.drawCenteredText(kStatusFont, titleY + 48, sub);

  // Three-column stats row (Time / Best Time / Played).
  // Spacing relaxed (v4) so the title / subtitle / stats / footnote read as
  // four distinct sections rather than a single dense block.
  const uint8_t s = GomokuStore::sizeIndex(board.boardSize);
  const GomokuStats stats = GomokuStore::loadStats();
  const int statsY = titleY + 120;
  constexpr int statsH = 96;
  const int sx = CONTENT_X;
  const int sw2 = sw - 2 * CONTENT_X;
  const int colW = sw2 / 3;

  renderer.drawLine(sx, statsY, sx + sw2, statsY, true);
  renderer.drawLine(sx, statsY + statsH, sx + sw2, statsY + statsH, true);

  auto drawStatCol = [&](int col, const char* label, const char* value) {
    const int cx = sx + col * colW;
    const int valW = renderer.getTextWidth(kStatValueFont, value);
    const int labW = renderer.getTextWidth(kStatusFont, label);
    // Inside statsH=96: value top at +24, label top at +64 — 40px between
    // tops keeps NOTOSANS_16 descenders clear and matches the relaxed rhythm.
    renderer.drawText(kStatValueFont, cx + (colW - valW) / 2, statsY + 24, value, true);
    renderer.drawText(kStatusFont, cx + (colW - labW) / 2, statsY + 64, label, true);
  };

  drawStatCol(0, tr(STR_GOMOKU_TIME), timeBuf);

  char bestBuf[16];
  if (s <= 1 && stats.bestTimeSec[s] > 0) {
    snprintf(bestBuf, sizeof(bestBuf), "%02u:%02u", static_cast<unsigned>(stats.bestTimeSec[s] / 60),
             static_cast<unsigned>(stats.bestTimeSec[s] % 60));
  } else {
    snprintf(bestBuf, sizeof(bestBuf), "--");
  }
  drawStatCol(1, tr(STR_GOMOKU_BEST_TIME), bestBuf);

  char playedBuf[16];
  uint16_t played = 0;
  if (s <= 1) {
    played = stats.blackWins[s] + stats.whiteWins[s] + stats.draws[s];
  }
  snprintf(playedBuf, sizeof(playedBuf), "%u", static_cast<unsigned>(played));
  drawStatCol(2, tr(STR_GOMOKU_PLAYED), playedBuf);

  // Footnote: cumulative record.
  if (s <= 1) {
    char rec[64];
    snprintf(rec, sizeof(rec), tr(STR_GOMOKU_RECORD_FMT), static_cast<unsigned>(stats.blackWins[s]),
             static_cast<unsigned>(stats.whiteWins[s]), static_cast<unsigned>(stats.draws[s]));
    renderer.drawCenteredText(kStatusFont, statsY + statsH + 28, rec);
  }

  drawFooter();
}
