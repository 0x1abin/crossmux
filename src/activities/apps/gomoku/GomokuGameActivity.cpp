#include "GomokuGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "GomokuAI.h"
#include "GomokuMenuActivity.h"

namespace {

// Centralized font roles, mirroring Sudoku.
constexpr int kStatusFont = UI_12_FONT_ID;           // title bar / mode line
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;      // win-screen big title
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;  // win-screen stat values + info-panel stat values

const char* modeLabel(GomokuMode m) {
  return (m == GomokuMode::VsAi) ? tr(STR_GOMOKU_MODE_AI) : tr(STR_GOMOKU_MODE_2P);
}

const char* difficultyShortLabel(GomokuAiLevel level) {
  switch (level) {
    case GomokuAiLevel::Easy:
      return "E";
    case GomokuAiLevel::Hard:
      return "H";
    default:
      return "M";
  }
}

}  // namespace

GomokuGameActivity::GomokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GomokuMode mode,
                                       uint8_t boardSize, bool resume, GomokuAiLevel aiLevel)
    : Activity("Gomoku", renderer, mappedInput), mode(mode), aiLevel(aiLevel), resumeRequested(resume) {
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
  saveDebouncer.clear();
  statsRecorded = false;
  resignedFlag = false;
  resignWinner = GomokuBoard::Stone::Empty;

  if (resumeRequested) {
    GomokuSaveSlot slot;
    if (GomokuStore::load(slot)) {
      board = slot.board;
      mode = slot.mode;
      aiLevel = slot.aiLevel;
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

  // If we resumed mid-game on AI's turn, queue a thinking pass so the AI
  // plays its move once we render the first frame.
  if (state == State::Playing && aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
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

    if (saveDebouncer.consumeIfDue(now)) {
      flushSave();
    }

    // Two-stage AI move: armed → render "Thinking…" once → search → place.
    // Forcing the render before the (possibly multi-second) search keeps the
    // e-ink screen responsive and gives the player feedback that input is
    // being handled.
    if (aiThinkingArmed) {
      if (!aiThinkingShown) {
        aiThinkingShown = true;
        requestUpdateAndWait();  // block until "Thinking…" is on screen
        return;
      }
      runAiTurn();
      aiThinkingArmed = false;
      aiThinkingShown = false;
      requestUpdate();
      return;
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
int GomokuGameActivity::boardOriginX() const {
  return (renderer.getScreenWidth() - (board.boardSize - 1) * boardPitch()) / 2;
}
int GomokuGameActivity::boardOriginY() const { return BOARD_AREA_Y + boardPitch() / 2; }
int GomokuGameActivity::stoneRadius() const { return (board.boardSize == 15) ? 12 : 20; }

void GomokuGameActivity::intersectionXY(uint8_t r, uint8_t c, int* x, int* y) const {
  *x = boardOriginX() + static_cast<int>(c) * boardPitch();
  *y = boardOriginY() + static_cast<int>(r) * boardPitch();
}

// ---------- Input ----------

void GomokuGameActivity::handleInputPlaying() {
  int touchX = 0;
  int touchY = 0;
  int row = 0;
  int column = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY) &&
      gameIntersectionFromPoint(boardOriginX(), boardOriginY(), boardPitch(), board.boardSize, board.boardSize, touchX,
                                touchY, row, column)) {
    cursorR = static_cast<uint8_t>(row);
    cursorC = static_cast<uint8_t>(column);
    requestUpdate();
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) &&
      gameIntersectionFromPoint(boardOriginX(), boardOriginY(), boardPitch(), board.boardSize, board.boardSize, touchX,
                                touchY, row, column)) {
    cursorR = static_cast<uint8_t>(row);
    cursorC = static_cast<uint8_t>(column);
    doPlace();
    requestUpdate();
    return;
  }
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
  gameMenu.handleInput(mappedInput, [this] { requestUpdate(); });
  if (!gameMenu.isActive() && state == State::GameMenu) resumeFromMenu();
}

void GomokuGameActivity::handleInputGameOver() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect again = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                         metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 0, 2);
  const Rect home = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                        metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 1, 2);
  if (mappedInput.wasTapInRect(again.x, again.y, again.width, again.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.replaceActivityWith<GomokuGameActivity>(mode, board.boardSize, false, aiLevel);
    return;
  }
  if (mappedInput.wasTapInRect(home.x, home.y, home.width, home.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivityWith<GomokuMenuActivity>();
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
    return;
  }
  if (aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }
}

bool GomokuGameActivity::aiToMove() const {
  return mode == GomokuMode::VsAi && state == State::Playing && board.winner == GomokuBoard::Stone::Empty &&
         !board.isFull() && board.nextTurn() == kAiSide;
}

void GomokuGameActivity::runAiTurn() {
  uint8_t mv = GomokuAI::chooseMove(board, kAiSide, aiLevel);
  uint8_t r = board.rowOf(mv);
  uint8_t c = board.colOf(mv);
  if (!board.placeStone(r, c)) {
    // Should never happen — chooseMove only picks empty cells. If it does,
    // skipping the move would leave nextTurn() pointing at the AI's color,
    // and the player's next placement would be misattributed. Find any
    // empty intersection as a passive fallback so the turn still advances.
    LOG_ERR("GMK", "AI returned illegal move idx=%u; using fallback", static_cast<unsigned>(mv));
    bool placed = false;
    const uint16_t total = static_cast<uint16_t>(board.boardSize) * board.boardSize;
    for (uint16_t i = 0; i < total; i++) {
      if (board.cells[i] == static_cast<uint8_t>(GomokuBoard::Stone::Empty)) {
        r = board.rowOf(static_cast<uint8_t>(i));
        c = board.colOf(static_cast<uint8_t>(i));
        if (board.placeStone(r, c)) {
          placed = true;
          break;
        }
      }
    }
    if (!placed) return;  // no empty cell — board is full, draw will be detected below
  }
  // Move cursor to AI's stone so the next player input picks up nearby —
  // and the cursor box visibly reflects the new state.
  cursorR = r;
  cursorC = c;
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
    saveDebouncer.clear();
    statsRecorded = true;
  }
}

void GomokuGameActivity::enterGameMenu() {
  state = State::GameMenu;
  const char* options[MENU_ITEM_COUNT] = {
      tr(STR_GAME_RESUME), tr(STR_GOMOKU_UNDO), tr(STR_GOMOKU_RESIGN), tr(STR_GAME_NEW_GAME), tr(STR_GAME_EXIT),
  };
  gameMenu.show(tr(STR_GAME_GAME_MENU), options, MENU_ITEM_COUNT, 0,
                [this](const int index) { runMenuItem(static_cast<uint8_t>(index)); });
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
        // In vs-AI mode, undoing once would just hand the turn back to the AI
        // which immediately replays — undo a second step so the player gets
        // their decision back. (Skip when the player just played the very
        // first move and the AI hasn't responded yet.)
        if (mode == GomokuMode::VsAi && board.moveCount > 0 && board.nextTurn() == kAiSide) {
          board.undo();
        }
        aiThinkingArmed = false;
        aiThinkingShown = false;
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
    case 3: {  // New Game (same mode + size + difficulty)
      const auto m = mode;
      const uint8_t bs = board.boardSize;
      const auto lv = aiLevel;
      GomokuStore::clear();
      activityManager.replaceActivityWith<GomokuGameActivity>(m, bs, false, lv);
      return;
    }
    case 4:  // Exit to menu
      flushSave();
      activityManager.replaceActivityWith<GomokuMenuActivity>();
      return;
  }
}

void GomokuGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void GomokuGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  saveDebouncer.clear();
  GomokuSaveSlot slot;
  slot.board = board;
  slot.mode = mode;
  slot.aiLevel = aiLevel;
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
      if (gameMenu.processRender(renderer, mappedInput)) return;
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
  renderer.drawLine(0, TITLE_BAR_H, w - 1, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  char left[64];
  if (mode == GomokuMode::VsAi) {
    snprintf(left, sizeof(left), "%s · %s [%s]", tr(STR_GOMOKU_TITLE), modeLabel(mode), difficultyShortLabel(aiLevel));
  } else {
    snprintf(left, sizeof(left), "%s · %s", tr(STR_GOMOKU_TITLE), modeLabel(mode));
  }
  renderer.drawText(kStatusFont, 12, y, left);

  // Right side: small stone dot + move count + time.
  // Dot reflects whose turn is next during play (or the winner once over).
  const GomokuBoard::Stone marker = (board.winner != GomokuBoard::Stone::Empty) ? board.winner : board.nextTurn();
  const bool markerIsBlack = (marker == GomokuBoard::Stone::Black);

  char timeStr[8];
  gameFormatElapsed(elapsedMs, timeStr, sizeof(timeStr));
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
}

// ---------- Mode line ----------

void GomokuGameActivity::drawModeLine() {
  if (!aiThinkingArmed) {
    return;
  }
  renderer.drawCenteredText(kStatusFont, MODE_LINE_Y, tr(STR_GOMOKU_AI_THINKING));
}

// ---------- Footer (button hints) ----------

void GomokuGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* leftLabel = "";
  const char* rightLabel = "";

  if (state == State::Playing) {
    backLabel = tr(STR_GOMOKU_MENU);
    confirmLabel = tr(STR_GOMOKU_PLACE);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_GAME_RESUME);
    confirmLabel = tr(STR_SELECT);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameOver) {
    backLabel = tr(STR_GOMOKU_MENU);
    confirmLabel = tr(STR_GOMOKU_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
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

  // Subtitle: mode · size · time · N moves
  char timeBuf[8];
  gameFormatElapsed(elapsedMs, timeBuf, sizeof(timeBuf));
  const char* sizeLabel = (board.boardSize == 9) ? tr(STR_GOMOKU_BOARD_9) : tr(STR_GOMOKU_BOARD_15);
  char sub[96];
  snprintf(sub, sizeof(sub), "%s · %s · %s · %u %s", modeLabel(mode), sizeLabel, timeBuf,
           static_cast<unsigned>(board.moveCount), tr(STR_GOMOKU_MOVES_SUFFIX));

  const uint8_t s = GomokuStore::sizeIndex(board.boardSize);
  const GomokuStats stats = GomokuStore::loadStats();

  char rec[64];
  const bool showRecord = s <= 1;
  if (showRecord) {
    snprintf(rec, sizeof(rec), tr(STR_GOMOKU_RECORD_FMT), static_cast<unsigned>(stats.blackWins[s]),
             static_cast<unsigned>(stats.whiteWins[s]), static_cast<unsigned>(stats.draws[s]));
  }

  constexpr int titleGap = 12;
  constexpr int sectionGap = 36;
  constexpr int statPadding = 16;
  constexpr int statTextGap = 12;
  constexpr int footnoteGap = 16;
  const int titleH = renderer.getTextHeight(kHeroFont);
  const int statusH = renderer.getTextHeight(kStatusFont);
  const int valueH = renderer.getTextHeight(kStatValueFont);
  const int statsH = statPadding + valueH + statTextGap + statusH + statPadding;
  const int blockH = titleH + titleGap + statusH + sectionGap + statsH + (showRecord ? footnoteGap + statusH : 0);
  const int availableBottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleY = gameCenteredBlockY(TITLE_BAR_H, availableBottom, blockH);
  const int subtitleY = titleY + titleH + titleGap;
  const int statsY = subtitleY + statusH + sectionGap;

  renderer.drawCenteredText(kHeroFont, titleY, titleStr, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(kStatusFont, subtitleY, sub);

  // Three-column stats row (Time / Best Time / Played).
  const int sx = CONTENT_X;
  const int sw2 = sw - 2 * CONTENT_X;
  const int colW = sw2 / 3;

  renderer.drawLine(sx, statsY, sx + sw2, statsY, true);
  renderer.drawLine(sx, statsY + statsH, sx + sw2, statsY + statsH, true);

  auto drawStatCol = [&](int col, const char* label, const char* value) {
    const int cx = sx + col * colW;
    const int valW = renderer.getTextWidth(kStatValueFont, value);
    const int labW = renderer.getTextWidth(kStatusFont, label);
    renderer.drawText(kStatValueFont, cx + (colW - valW) / 2, statsY + statPadding, value, true);
    renderer.drawText(kStatusFont, cx + (colW - labW) / 2, statsY + statPadding + valueH + statTextGap, label, true);
  };

  drawStatCol(0, tr(STR_GAME_TIME), timeBuf);

  char bestBuf[16];
  if (s <= 1 && stats.bestTimeSec[s] > 0) {
    snprintf(bestBuf, sizeof(bestBuf), "%02u:%02u", static_cast<unsigned>(stats.bestTimeSec[s] / 60),
             static_cast<unsigned>(stats.bestTimeSec[s] % 60));
  } else {
    snprintf(bestBuf, sizeof(bestBuf), "--");
  }
  drawStatCol(1, tr(STR_GAME_BEST_TIME), bestBuf);

  char playedBuf[16];
  uint16_t played = 0;
  if (s <= 1) {
    played = stats.blackWins[s] + stats.whiteWins[s] + stats.draws[s];
  }
  snprintf(playedBuf, sizeof(playedBuf), "%u", static_cast<unsigned>(played));
  drawStatCol(2, tr(STR_GOMOKU_PLAYED), playedBuf);

  if (showRecord) {
    renderer.drawCenteredText(kStatusFont, statsY + statsH + footnoteGap, rec);
  }

  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 0, 2),
        tr(STR_GOMOKU_AGAIN));
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 1, 2),
        tr(STR_GAME_HOME));
  }

  drawFooter();
}
