#include "GoGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "GoMenuActivity.h"
#include "activities/apps/GameUi.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Centralized font roles, mirroring Gomoku.
constexpr int kStatusFont = UI_12_FONT_ID;
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;

// Encodings: PASS move in the move log (board cells are 0..80).
constexpr uint8_t kPassMove = 81;
constexpr uint8_t kMaxMoveLog = 100;

const char* modeLabel(GoMode m) {
  return (m == GoMode::VsAi) ? tr(STR_GO_MODE_AI) : tr(STR_GO_MODE_2P);
}

const char* difficultyLabel(GoDifficulty d) {
  switch (d) {
    case GoDifficulty::Kyu20: return tr(STR_GO_DIFF_20K);
    case GoDifficulty::Kyu18: return tr(STR_GO_DIFF_18K);
    case GoDifficulty::Kyu15: return tr(STR_GO_DIFF_15K);
    case GoDifficulty::Kyu12: return tr(STR_GO_DIFF_12K);
    case GoDifficulty::Kyu10: return tr(STR_GO_DIFF_10K);
    case GoDifficulty::Kyu7: return tr(STR_GO_DIFF_7K);
    default: return tr(STR_GO_DIFF_5K);
  }
}

}  // namespace

GoGameActivity::GoGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, GoMode mode,
                               GoDifficulty difficulty, bool resume)
    : Activity("Go", renderer, mappedInput), mode(mode), difficulty(difficulty), resumeRequested(resume) {}

void GoGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  state = State::Playing;
  elapsedMs = 0;
  lastTickMs = millis();
  saveDebouncer.clear();
  statsRecorded = false;
  aiThinkingArmed = false;
  aiThinkingShown = false;
  lastAiPct = 0;
  lastAiVisits = 0;
  lastAiMs = 0;
  hasAiStats = false;

  if (resumeRequested) {
    GoSaveSlot slot;
    if (GoStore::load(slot)) {
      game = slot.game;
      mode = slot.mode;
      difficulty = slot.difficulty;
      cursorX = (slot.cursorX < kBoardSize) ? slot.cursorX : 4;
      cursorY = (slot.cursorY < kBoardSize) ? slot.cursorY : 4;
      elapsedMs = static_cast<uint32_t>(slot.elapsedSec) * 1000u;
      if (game.isGameOver() || game.resignedBy) {
        state = State::GameOver;
        statsRecorded = true;  // recorded on the original session
      }
    } else {
      LOG_ERR("GO", "Resume save unreadable; clearing and starting fresh");
      GoStore::clear();
      resumeRequested = false;
      startNewGame();
      GoStore::recordStart();
    }
  } else {
    startNewGame();
    GoStore::recordStart();
  }

  if (state == State::Playing && aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }

  requestUpdate();
}

void GoGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

// Fresh game: reset engine + apply the difficulty's color/handicap/komi.
// Mirrors ArduGO's startGame() + difficulty application.
void GoGameActivity::applyHandicap() {
  const uint8_t d = static_cast<uint8_t>(difficulty);
  const uint8_t hc = static_cast<uint8_t>((d & 1) + 1);   // BLACK(1) or WHITE(2)
  const uint8_t hs = static_cast<uint8_t>((d >> 1) & 3);  // 0..3 handicap stones
  const uint8_t kp = (d & 8) ? 13 : 1;                    // komi in half-points
  game.aiPlayer = static_cast<uint8_t>(3 - hc);
  game.kpieces = kp;
  if (hs) {
    // Engine's 9x9 handicap points, facing pair first then third corner.
    static const uint8_t kHpts[3] = {6 + 2 * 9, 2 + 6 * 9, 6 + 6 * 9};
    for (uint8_t i = 0; i < hs; i++) {
      const uint8_t p = kHpts[i];
      game.set(static_cast<uint8_t>(p % 9), static_cast<uint8_t>(p / 9), BLACK);
    }
    game.lastMove = game.koCap = 0xFF;
    game.koExact = 0;
    game.turn = WHITE;
    ai.notifyPass();  // clear engine first-move state; board is non-empty
  }
}

void GoGameActivity::startNewGame() {
  game.reset();
  ai.reset();
  applyHandicap();
  cursorX = cursorY = 4;
}

void GoGameActivity::loop() {
  if (state == State::Playing) {
    const uint32_t now = millis();
    if (now > lastTickMs) {
      elapsedMs += now - lastTickMs;
    }
    lastTickMs = now;

    if (saveDebouncer.consumeIfDue(now)) {
      flushSave();
    }

    // Two-stage AI move: armed → render "Thinking…" → search → move.
    if (aiThinkingArmed) {
      if (!aiThinkingShown) {
        aiThinkingShown = true;
        requestUpdateAndWait();
        return;
      }
      runAiTurn();
      aiThinkingArmed = false;
      aiThinkingShown = false;
      if (state == State::Playing) requestUpdate();
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

int GoGameActivity::boardPitch() const { return 50; }
int GoGameActivity::boardOriginX() const {
  return (renderer.getScreenWidth() - (kBoardSize - 1) * boardPitch()) / 2;
}
int GoGameActivity::boardOriginY() const { return BOARD_AREA_Y + boardPitch() / 2; }
int GoGameActivity::stoneRadius() const { return 20; }

void GoGameActivity::intersectionXY(uint8_t r, uint8_t c, int* x, int* y) const {
  *x = boardOriginX() + static_cast<int>(c) * boardPitch();
  *y = boardOriginY() + static_cast<int>(r) * boardPitch();
}

Rect GoGameActivity::boardTouchRect() const {
  const int pitch = boardPitch();
  const int ox = boardOriginX();
  const int oy = boardOriginY();
  const int len = (kBoardSize - 1) * pitch;
  const int pad = pitch / 2;
  return Rect{ox - pad, oy - pad, len + 2 * pad, len + 2 * pad};
}

int GoGameActivity::actionBarH() const {
  return std::max(ACTION_BAR_MIN_H, renderer.getScreenHeight() * ACTION_BAR_H_FRAC / 100);
}

int GoGameActivity::actionBarY() const {
  const int h = renderer.getScreenHeight();
  return h - actionBarH() - std::max(4, h * ACTION_BAR_BOTTOM_FRAC / 100);
}

Rect GoGameActivity::actionBarRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = std::max(8, metrics.contentSidePadding);
  return Rect{side, actionBarY(), renderer.getScreenWidth() - 2 * side, actionBarH()};
}

Rect GoGameActivity::placeButtonRect() const {
  const Rect bar = actionBarRect();
  return Rect{bar.x, bar.y, bar.width / 2, bar.height};
}

Rect GoGameActivity::passButtonRect() const {
  const Rect bar = actionBarRect();
  return Rect{bar.x + bar.width / 2, bar.y, bar.width - bar.width / 2, bar.height};
}

// ---------- Input ----------

void GoGameActivity::handleInputPlaying() {
  int touchX = 0;
  int touchY = 0;
  const Rect boardRect = boardTouchRect();
  const auto inRect = [](const Rect& r, const int x, const int y) {
    return r.width > 0 && r.height > 0 && x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
  };
  const auto aimAt = [&](const int x, const int y) {
    int row = 0;
    int column = 0;
    if (!gameIntersectionFromPoint(boardOriginX(), boardOriginY(), boardPitch(), kBoardSize, kBoardSize, x, y, row,
                                   column)) {
      return false;
    }
    if (cursorX == static_cast<uint8_t>(column) && cursorY == static_cast<uint8_t>(row)) return false;
    cursorX = static_cast<uint8_t>(column);
    cursorY = static_cast<uint8_t>(row);
    requestUpdate();
    return true;
  };

  if (mappedInput.wasScreenTapped(touchX, touchY) && inRect(placeButtonRect(), touchX, touchY)) {
    doPlace();
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) && inRect(passButtonRect(), touchX, touchY)) {
    doPass();
    return;
  }
  if (mappedInput.wasScreenTouchDown(touchX, touchY) && inRect(boardRect, touchX, touchY)) {
    aimAt(touchX, touchY);
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) && inRect(boardRect, touchX, touchY)) {
    aimAt(touchX, touchY);
    return;  // aim only — the stone is committed by Place / Confirm
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveCursor(0, -1);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveCursor(0, 1);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveCursor(-1, 0);
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveCursor(1, 0);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    doPlace();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    enterGameMenu();
    requestUpdate();
  }
}

void GoGameActivity::handleInputGameMenu() {
  gameMenu.handleInput(mappedInput, [this] { requestUpdate(); });
  if (!gameMenu.isActive() && state == State::GameMenu) resumeFromMenu();
}

void GoGameActivity::handleInputGameOver() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect again = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                         metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 0, 2);
  const Rect home = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                        metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 1, 2);
  if (mappedInput.wasTapInRect(again.x, again.y, again.width, again.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.replaceActivityWith<GoGameActivity>(mode, difficulty, false);
    return;
  }
  if (mappedInput.wasTapInRect(home.x, home.y, home.width, home.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivityWith<GoMenuActivity>();
  }
}

void GoGameActivity::moveCursor(int dr, int dc) {
  const int n = kBoardSize;
  cursorX = static_cast<uint8_t>((cursorX + dc + n) % n);
  cursorY = static_cast<uint8_t>((cursorY + dr + n) % n);
}

void GoGameActivity::doPlace() {
  if (state != State::Playing) return;
  if (!game.playMove(cursorX, cursorY)) return;  // occupied / suicide / ko
  pushMove(game.lastMove);
  scheduleSave();
  if (game.isGameOver()) {
    onGameOver();
    return;
  }
  if (aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }
}

void GoGameActivity::doPass() {
  if (state != State::Playing) return;
  game.pass();
  pushMove(kPassMove);
  scheduleSave();
  if (game.isGameOver()) {
    onGameOver();
    return;
  }
  if (aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }
}

bool GoGameActivity::aiToMove() {
  return mode == GoMode::VsAi && state == State::Playing && !game.isGameOver() && !game.resignedBy &&
         game.turn == game.aiPlayer;
}

void GoGameActivity::runAiTurn() {
  // Opening book / NN. chooseMove applies the move itself when it hits.
  bool moved = false;
  if (ai.chooseMove(game)) {
    moved = true;
  } else {
    const uint32_t t0 = millis();
    ai.think(game);
    lastAiMs = millis() - t0;
    hasAiStats = true;
    lastAiPct = ai.statPct;
    lastAiVisits = ai.statVisits;
    if (ai.resigned) {
      game.resignedBy = game.aiPlayer;
      onGameOver();
      return;
    }
    uint8_t x = 0;
    uint8_t y = 0;
    if (ai.bestMove(game, x, y)) {
      game.playMove(x, y);
      ai.notifyMove(x, y);
      moved = true;
    } else {
      game.pass();
      ai.notifyPass();
      pushMove(kPassMove);
      if (game.isGameOver()) {
        onGameOver();
        return;
      }
      return;
    }
  }
  if (moved && game.lastMove != 0xFF) {
    pushMove(game.lastMove);
  }
  scheduleSave();
  if (game.isGameOver()) {
    onGameOver();
  }
}

// Move log: replay buffer for Undo. Pass moves are stored as kPassMove.
void GoGameActivity::pushMove(uint8_t idx) {
  if (moveCount < kMaxMoveLog) moveLog[moveCount++] = idx;
}

void GoGameActivity::undoMoves(uint8_t n) {
  if (moveCount == 0) return;
  if (n > moveCount) n = moveCount;
  moveCount -= n;
  // Rebuild the game from scratch: reset + handicap + replay remaining.
  startNewGame();
  for (uint16_t i = 0; i < moveCount; i++) {
    if (moveLog[i] == kPassMove) {
      game.pass();
    } else {
      const uint8_t p = moveLog[i];
      if (!game.playMove(static_cast<uint8_t>(p % 9), static_cast<uint8_t>(p / 9))) {
        LOG_ERR("GO", "Undo replay failed at %u", static_cast<unsigned>(i));
        break;
      }
    }
  }
  aiThinkingArmed = false;
  aiThinkingShown = false;
  scheduleSave();
}

void GoGameActivity::onGameOver() {
  if (state == State::GameOver) return;
  state = State::GameOver;
  if (!statsRecorded) {
    // Natural end (two passes): playout-vote dead stones off, then score.
    if (!game.resignedBy && game.isGameOver()) {
      ai.scoreDead(game);
      game.computeScore();
    }
    const uint8_t winner = game.resignedBy ? static_cast<uint8_t>(3 - game.resignedBy)
                                           : (game.blackWins() ? BLACK : WHITE);
    GoStore::recordWin(winner);
    GoStore::clear();
    saveDebouncer.clear();
    statsRecorded = true;
  }
}

void GoGameActivity::enterGameMenu() {
  state = State::GameMenu;
  const char* options[MENU_ITEM_COUNT] = {
      tr(STR_GAME_RESUME), tr(STR_GO_UNDO), tr(STR_GO_PASS), tr(STR_GO_RESIGN), tr(STR_GAME_NEW_GAME),
      tr(STR_GAME_EXIT),
  };
  gameMenu.show(tr(STR_GO_MENU), options, MENU_ITEM_COUNT, 0,
                [this](const int index) { runMenuItem(static_cast<uint8_t>(index)); });
}

void GoGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();
}

void GoGameActivity::runMenuItem(uint8_t i) {
  switch (i) {
    case 0:  // Resume
      resumeFromMenu();
      return;
    case 1:  // Undo (two plies in vs-AI so the player regains the initiative)
      if (moveCount > 0) {
        undoMoves((mode == GoMode::VsAi) ? 2 : 1);
      }
      resumeFromMenu();
      return;
    case 2:  // Pass
      resumeFromMenu();
      doPass();
      return;
    case 3:  // Resign — side to move resigns; the other side wins.
      game.resignedBy = game.turn;
      onGameOver();
      return;
    case 4:  // New Game (same mode + difficulty)
      GoStore::clear();
      activityManager.replaceActivityWith<GoGameActivity>(mode, difficulty, false);
      return;
    case 5:  // Exit to menu
      flushSave();
      activityManager.replaceActivityWith<GoMenuActivity>();
      return;
  }
}

void GoGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void GoGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  saveDebouncer.clear();
  GoSaveSlot slot;
  slot.game = game;
  slot.mode = mode;
  slot.difficulty = difficulty;
  slot.cursorX = cursorX;
  slot.cursorY = cursorY;
  slot.elapsedSec = static_cast<uint16_t>(elapsedMs / 1000);
  if (!GoStore::save(slot)) {
    LOG_ERR("GO", "Failed to write save");
  }
}

// ---------- Render dispatch ----------

void GoGameActivity::render(RenderLock&&) {
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

void GoGameActivity::renderPlaying() {
  drawTitleBar();
  drawBoard();
  drawInfoPanel();
  drawThinkingNotice();
  if (mappedInput.hasTouch()) {
    GUI.drawActionButton(renderer, placeButtonRect(), tr(STR_GO_PLACE), true);
    GUI.drawActionButton(renderer, passButtonRect(), tr(STR_GO_PASS), false);
  }
  drawFooter();
}

// ---------- Title bar ----------

void GoGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H, w - 1, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  char left[64];
  if (mode == GoMode::VsAi) {
    snprintf(left, sizeof(left), "%s · %s [%s]", tr(STR_GO_TITLE), modeLabel(mode), difficultyLabel(difficulty));
  } else {
    snprintf(left, sizeof(left), "%s · %s", tr(STR_GO_TITLE), modeLabel(mode));
  }
  renderer.drawText(kStatusFont, 12, y, left);

  // Right side: whose turn + move count + time.
  const bool markerIsBlack = (game.turn == BLACK);
  char timeStr[8];
  gameFormatElapsed(elapsedMs, timeStr, sizeof(timeStr));
  char tail[24];
  snprintf(tail, sizeof(tail), "%u · %s", static_cast<unsigned>(moveCount), timeStr);
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

void GoGameActivity::drawBoard() {
  const int n = kBoardSize;
  const int pitch = boardPitch();
  const int ox = boardOriginX();
  const int oy = boardOriginY();
  const int len = (n - 1) * pitch;

  for (int i = 0; i < n; i++) {
    renderer.drawLine(ox, oy + i * pitch, ox + len, oy + i * pitch, true);
    renderer.drawLine(ox + i * pitch, oy, ox + i * pitch, oy + len, true);
  }

  // 9x9 hoshi: four corners + centre.
  static constexpr uint8_t kHoshi9[5][2] = {{2, 2}, {2, 6}, {4, 4}, {6, 2}, {6, 6}};
  for (int k = 0; k < 5; k++) {
    const int hx = ox + kHoshi9[k][1] * pitch;
    const int hy = oy + kHoshi9[k][0] * pitch;
    renderer.fillRoundedRect(hx - 3, hy - 3, 7, 7, 3, Color::Black);
  }

  // Stones.
  const int sr = stoneRadius();
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      const uint8_t v = game.at(static_cast<uint8_t>(c), static_cast<uint8_t>(r));
      if (v == EMPTY) continue;
      const int sx = ox + c * pitch;
      const int sy = oy + r * pitch;
      drawStone(sx, sy, sr, v == BLACK);
    }
  }

  // Last-move marker.
  if (game.lastMove != 0xFF) {
    const uint8_t lc = static_cast<uint8_t>(game.lastMove % 9);
    const uint8_t lr = static_cast<uint8_t>(game.lastMove / 9);
    const int sx = ox + lc * pitch;
    const int sy = oy + lr * pitch;
    const bool isBlack = (game.board[game.lastMove] == BLACK);
    const Color dotColor = isBlack ? Color::White : Color::Black;
    renderer.fillRoundedRect(sx - 2, sy - 2, 5, 5, 2, dotColor);
  }

  // Cursor.
  if (state == State::Playing && !game.isGameOver()) {
    int cx, cy;
    intersectionXY(cursorY, cursorX, &cx, &cy);
    constexpr int half = 12;
    renderer.drawRect(cx - half, cy - half, 2 * half, 2 * half, 2, true);
  }
}

void GoGameActivity::drawStone(int cx, int cy, int radius, bool isBlack) const {
  const int side = 2 * radius + 1;
  if (isBlack) {
    renderer.fillRoundedRect(cx - radius, cy - radius, side, side, radius, Color::Black);
  } else {
    renderer.fillRoundedRect(cx - radius, cy - radius, side, side, radius, Color::White);
    renderer.drawRoundedRect(cx - radius, cy - radius, side, side, 2, radius, true);
  }
}

// ---------- Info panel ----------

void GoGameActivity::drawInfoPanel() {
  const int sw = renderer.getScreenWidth();

  constexpr int statH = 44;
  constexpr int cellW = 160;
  constexpr int cellGap = 24;
  const int statY = std::max(BOARD_AREA_Y, actionBarY() - statH - 6);
  const int totalW = 2 * cellW + cellGap;
  const int statXStart = (sw - totalW) / 2;

  const bool showTurn = !game.isGameOver() && !game.resignedBy;
  const uint8_t activeSide = showTurn ? game.turn : EMPTY;

  auto drawStatCell = [&](int xLeft, uint8_t color) {
    renderer.drawRect(xLeft, statY, cellW, statH, true);
    char numBuf[8];
    // Captures: captures[0] = black's captures, captures[1] = white's.
    const uint16_t caps = (color == BLACK) ? game.captures[0] : game.captures[1];
    snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(caps));
    const int valH = renderer.getTextHeight(kStatValueFont);
    const int valW = renderer.getTextWidth(kStatValueFont, numBuf);
    constexpr int gap = 10;
    const int groupW = 2 * stoneRadius() + gap + valW;
    const int gx = xLeft + (cellW - groupW) / 2;
    const int cy = statY + statH / 2;
    drawStone(gx + stoneRadius(), cy, stoneRadius(), color == BLACK);
    constexpr int valOpticalBias = 4;
    renderer.drawText(kStatValueFont, gx + 2 * stoneRadius() + gap, cy - valH / 2 - valOpticalBias, numBuf);

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

  drawStatCell(statXStart, BLACK);
  drawStatCell(statXStart + cellW + cellGap, WHITE);
}

void GoGameActivity::drawThinkingNotice() {
  if (!aiThinkingArmed) return;
  renderer.drawCenteredText(kStatusFont, std::max(0, actionBarY() - renderer.getTextHeight(kStatusFont) - 4),
                            tr(STR_GO_AI_THINKING));
}

// ---------- Footer (button hints) ----------

void GoGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* leftLabel = "";
  const char* rightLabel = "";

  if (state == State::Playing) {
    backLabel = tr(STR_GO_MENU);
    confirmLabel = tr(STR_GO_PLACE);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_GAME_RESUME);
    confirmLabel = tr(STR_SELECT);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameOver) {
    backLabel = tr(STR_GO_MENU);
    confirmLabel = tr(STR_GO_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------- Game Over screen ----------

void GoGameActivity::renderGameOver() {
  drawTitleBar();
  const int sw = renderer.getScreenWidth();

  // Outcome: resignation → the other side; otherwise scored area.
  const char* titleStr = nullptr;
  if (game.resignedBy) {
    titleStr = (game.resignedBy == WHITE) ? tr(STR_GO_BLACK_WINS_RESIGN) : tr(STR_GO_WHITE_WINS_RESIGN);
  } else {
    titleStr = game.blackWins() ? tr(STR_GO_BLACK_WINS) : tr(STR_GO_WHITE_WINS);
  }

  char timeBuf[8];
  gameFormatElapsed(elapsedMs, timeBuf, sizeof(timeBuf));
  char komiBuf[16];
  snprintf(komiBuf, sizeof(komiBuf), "%s %s", tr(STR_GO_KOMI),
           (game.kpieces == 13) ? "6.5" : "0.5");
  char sub[96];
  if (mode == GoMode::VsAi) {
    snprintf(sub, sizeof(sub), "%s [%s] · %s · %s · %u %s", modeLabel(mode), difficultyLabel(difficulty), komiBuf,
             timeBuf, static_cast<unsigned>(moveCount), tr(STR_GO_MOVES_SUFFIX));
  } else {
    snprintf(sub, sizeof(sub), "%s · %s · %s · %u %s", modeLabel(mode), komiBuf, timeBuf,
             static_cast<unsigned>(moveCount), tr(STR_GO_MOVES_SUFFIX));
  }

  const GoStore::GoStats stats = GoStore::loadStats();
  char rec[64];
  snprintf(rec, sizeof(rec), tr(STR_GO_RECORD_FMT), static_cast<unsigned>(stats.blackWins),
           static_cast<unsigned>(stats.whiteWins), static_cast<unsigned>(stats.startedCount));

  constexpr int titleGap = 12;
  constexpr int sectionGap = 36;
  constexpr int statPadding = 16;
  constexpr int statTextGap = 12;
  constexpr int footnoteGap = 16;
  const int titleH = renderer.getTextHeight(kHeroFont);
  const int statusH = renderer.getTextHeight(kStatusFont);
  const int valueH = renderer.getTextHeight(kStatValueFont);
  const int statsH = statPadding + valueH + statTextGap + statusH + statPadding;
  const int blockH = titleH + titleGap + statusH + sectionGap + statsH + footnoteGap + statusH;
  const int availableBottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleY = gameCenteredBlockY(TITLE_BAR_H, availableBottom, blockH);
  const int subtitleY = titleY + titleH + titleGap;
  const int statsY = subtitleY + statusH + sectionGap;

  renderer.drawCenteredText(kHeroFont, titleY, titleStr, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(kStatusFont, subtitleY, sub);

  // Two-column stats row: Time / AI moves (last think) when applicable.
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

  char aiBuf[16];
  if (mode == GoMode::VsAi && hasAiStats) {
    snprintf(aiBuf, sizeof(aiBuf), "%u%%/%u", static_cast<unsigned>(lastAiPct), static_cast<unsigned>(lastAiVisits));
  } else {
    snprintf(aiBuf, sizeof(aiBuf), "--");
  }
  drawStatCol(1, tr(STR_GO_AI_LAST), aiBuf);

  char playedBuf[16];
  snprintf(playedBuf, sizeof(playedBuf), "%u", static_cast<unsigned>(stats.startedCount));
  drawStatCol(2, tr(STR_GO_PLAYED), playedBuf);

  renderer.drawCenteredText(kStatusFont, statsY + statsH + footnoteGap, rec);

  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 0, 2),
        tr(STR_GO_AGAIN));
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 1, 2),
        tr(STR_GAME_HOME));
  }

  drawFooter();
}
