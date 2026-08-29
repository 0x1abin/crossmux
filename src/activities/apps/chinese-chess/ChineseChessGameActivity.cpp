#include "ChineseChessGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "ChineseChessMenuActivity.h"

namespace {

using Side = ChineseChessBoard::Side;
using Kind = ChineseChessBoard::Kind;
using Move = ChineseChessBoard::Move;

constexpr int kStatusFont = UI_12_FONT_ID;
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;

// UTF-8 strings for the 14 Chinese chess piece characters.
// Indexed as [side][kind] where side 0=Red, 1=Black; kind: 0=King ... 6=Pawn.
constexpr const char* kPieceChar[2][7] = {
    {"帅", "仕", "相", "馬", "俥", "砲", "兵"},  // Red
    {"将", "士", "象", "马", "车", "炮", "卒"},  // Black
};

const char* modeLabel(ChineseChessMode m) {
  return (m == ChineseChessMode::VsAi) ? tr(STR_CHINESE_CHESS_MODE_AI) : tr(STR_CHINESE_CHESS_MODE_2P);
}

const char* difficultyShortLabel(ChineseChessAiLevel level) {
  switch (level) {
    case ChineseChessAiLevel::Easy:
      return "E";
    case ChineseChessAiLevel::Hard:
      return "H";
    default:
      return "M";
  }
}

const char* sideLabel(Side s) { return (s == Side::Red) ? tr(STR_CHINESE_CHESS_RED) : tr(STR_CHINESE_CHESS_BLACK); }

}  // namespace

ChineseChessGameActivity::ChineseChessGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   ChineseChessMode mode, bool resume, ChineseChessAiLevel aiLevel)
    : Activity("ChineseChess", renderer, mappedInput), mode(mode), aiLevel(aiLevel), resumeRequested(resume) {
  board.reset();
  cursorR = 9;
  cursorC = 4;
}

void ChineseChessGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  state = State::Playing;
  redElapsedMs = 0;
  blackElapsedMs = 0;
  lastTickMs = millis();
  saveDebouncer.clear();
  statsRecorded = false;
  resignedFlag = false;
  resignWinner = Side::Red;
  hasSelection = false;
  legalCount = 0;

  if (resumeRequested) {
    ChineseChessSaveSlot slot;
    if (ChineseChessStore::load(slot)) {
      board = slot.board;
      mode = slot.mode;
      aiLevel = slot.aiLevel;
      cursorR = (slot.cursorR < ChineseChessBoard::RANKS) ? slot.cursorR : 9;
      cursorC = (slot.cursorC < ChineseChessBoard::FILES) ? slot.cursorC : 4;
      redElapsedMs = static_cast<uint32_t>(slot.redElapsedSec) * 1000u;
      blackElapsedMs = static_cast<uint32_t>(slot.blackElapsedSec) * 1000u;
      if (slot.hasSelection && slot.selR < ChineseChessBoard::RANKS && slot.selC < ChineseChessBoard::FILES) {
        selectAt(slot.selR, slot.selC);
      }
      if (board.isOver()) {
        state = State::GameOver;
        statsRecorded = true;
      }
    } else {
      LOG_ERR("XQI", "Resume save unreadable; clearing and starting fresh");
      ChineseChessStore::clear();
      resumeRequested = false;
      ChineseChessStore::recordStart();
    }
  } else {
    ChineseChessStore::recordStart();
  }

  if (state == State::Playing && aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }

  requestUpdate();
}

void ChineseChessGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void ChineseChessGameActivity::loop() {
  if (state == State::Playing) {
    const uint32_t now = millis();
    if (now > lastTickMs) {
      const uint32_t delta = now - lastTickMs;
      if (board.nextTurn() == Side::Red)
        redElapsedMs += delta;
      else
        blackElapsedMs += delta;
    }
    lastTickMs = now;

    if (saveDebouncer.consumeIfDue(now)) {
      flushSave();
    }

    if (aiThinkingArmed) {
      if (!aiThinkingShown) {
        aiThinkingShown = true;
        requestUpdateAndWait();
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

int ChineseChessGameActivity::boardOriginX() const {
  return (renderer.getScreenWidth() - BOARD_PITCH * (ChineseChessBoard::FILES - 1)) / 2;
}

void ChineseChessGameActivity::cellXY(uint8_t r, uint8_t c, int* x, int* y) const {
  *x = boardOriginX() + static_cast<int>(c) * BOARD_PITCH;
  *y = BOARD_ORIGIN_Y + static_cast<int>(r) * BOARD_PITCH;
}

// ---------- Input ----------

void ChineseChessGameActivity::handleInputPlaying() {
  int touchX = 0;
  int touchY = 0;
  int row = 0;
  int column = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY) &&
      gameIntersectionFromPoint(boardOriginX(), BOARD_ORIGIN_Y, BOARD_PITCH, ChineseChessBoard::RANKS,
                                ChineseChessBoard::FILES, touchX, touchY, row, column)) {
    cursorR = static_cast<uint8_t>(row);
    cursorC = static_cast<uint8_t>(column);
    requestUpdate();
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) &&
      gameIntersectionFromPoint(boardOriginX(), BOARD_ORIGIN_Y, BOARD_PITCH, ChineseChessBoard::RANKS,
                                ChineseChessBoard::FILES, touchX, touchY, row, column)) {
    cursorR = static_cast<uint8_t>(row);
    cursorC = static_cast<uint8_t>(column);
    doConfirm();
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
    doConfirm();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (hasSelection) {
      clearSelection();
      requestUpdate();
    } else {
      enterGameMenu();
      requestUpdate();
    }
  }
}

void ChineseChessGameActivity::handleInputGameMenu() {
  gameMenu.handleInput(mappedInput, [this] { requestUpdate(); });
  if (!gameMenu.isActive() && state == State::GameMenu) resumeFromMenu();
}

void ChineseChessGameActivity::handleInputGameOver() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect again = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                         metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 0, 2);
  const Rect home = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                        metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 1, 2);
  if (mappedInput.wasTapInRect(again.x, again.y, again.width, again.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activityManager.replaceActivityWith<ChineseChessGameActivity>(mode, false, aiLevel);
    return;
  }
  if (mappedInput.wasTapInRect(home.x, home.y, home.width, home.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivityWith<ChineseChessMenuActivity>();
  }
}

void ChineseChessGameActivity::moveCursor(int dr, int dc) {
  const int r = (cursorR + dr + ChineseChessBoard::RANKS) % ChineseChessBoard::RANKS;
  const int c = (cursorC + dc + ChineseChessBoard::FILES) % ChineseChessBoard::FILES;
  cursorR = static_cast<uint8_t>(r);
  cursorC = static_cast<uint8_t>(c);
}

bool ChineseChessGameActivity::isLegalTarget(uint8_t r, uint8_t c) const {
  for (uint8_t i = 0; i < legalCount; i++) {
    if (legalCache[i].to == ChineseChessBoard::idx(r, c)) return true;
  }
  return false;
}

void ChineseChessGameActivity::selectAt(uint8_t r, uint8_t c) {
  selR = r;
  selC = c;
  hasSelection = true;
  legalCount =
      board.generateLegalMovesFrom(ChineseChessBoard::idx(r, c), legalCache, ChineseChessBoard::MAX_LEGAL_MOVES);
}

void ChineseChessGameActivity::clearSelection() {
  hasSelection = false;
  selR = ChineseChessBoard::INVALID_IDX;
  selC = ChineseChessBoard::INVALID_IDX;
  legalCount = 0;
}

void ChineseChessGameActivity::doConfirm() {
  if (board.isOver()) return;
  const Side stm = board.nextTurn();
  const uint8_t cellIdx = ChineseChessBoard::idx(cursorR, cursorC);
  const uint8_t v = board.cells[cellIdx];

  if (!hasSelection) {
    // Pick own piece.
    if (v == ChineseChessBoard::Empty) return;
    if (ChineseChessBoard::sideOf(v) != stm) return;
    selectAt(cursorR, cursorC);
    return;
  }

  // Re-select another own piece if cursor lands on one.
  if (v != ChineseChessBoard::Empty && ChineseChessBoard::sideOf(v) == stm) {
    selectAt(cursorR, cursorC);
    return;
  }

  // Try to move to cursor.
  if (!isLegalTarget(cursorR, cursorC)) return;
  Move m;
  m.from = ChineseChessBoard::idx(selR, selC);
  m.to = cellIdx;
  if (!board.makeMove(m)) return;
  clearSelection();
  scheduleSave();

  // Detect natural game-over for the new side-to-move.
  board.updateResult();
  if (board.isOver()) {
    onGameOver();
    return;
  }
  if (aiToMove()) {
    aiThinkingArmed = true;
    aiThinkingShown = false;
  }
}

bool ChineseChessGameActivity::aiToMove() const {
  return mode == ChineseChessMode::VsAi && state == State::Playing && !board.isOver() && board.nextTurn() == kAiSide;
}

void ChineseChessGameActivity::runAiTurn() {
  Move mv = ChineseChessAI::chooseMove(board, kAiSide, aiLevel);
  if (mv.from == ChineseChessBoard::INVALID_IDX) {
    LOG_ERR("XQI", "AI returned no move");
    return;
  }
  if (!board.makeMove(mv)) {
    LOG_ERR("XQI", "AI illegal move from=%u to=%u", static_cast<unsigned>(mv.from), static_cast<unsigned>(mv.to));
    return;
  }
  cursorR = ChineseChessBoard::rowOf(mv.to);
  cursorC = ChineseChessBoard::colOf(mv.to);
  scheduleSave();
  board.updateResult();
  if (board.isOver()) {
    onGameOver();
  }
}

void ChineseChessGameActivity::onGameOver() {
  if (state == State::GameOver) return;
  state = State::GameOver;
  if (!statsRecorded) {
    const uint32_t totalSec = (redElapsedMs + blackElapsedMs) / 1000;
    const uint16_t cap = static_cast<uint16_t>(totalSec > 0xFFFF ? 0xFFFF : totalSec);
    if (resignedFlag) {
      ChineseChessStore::recordWin(resignWinner, cap);
    } else if (board.result == ChineseChessBoard::Result::RedWins) {
      ChineseChessStore::recordWin(Side::Red, cap);
    } else if (board.result == ChineseChessBoard::Result::BlackWins) {
      ChineseChessStore::recordWin(Side::Black, cap);
    } else {
      ChineseChessStore::recordDraw();
    }
    ChineseChessStore::clear();
    saveDebouncer.clear();
    statsRecorded = true;
  }
}

void ChineseChessGameActivity::enterGameMenu() {
  state = State::GameMenu;
  const char* options[MENU_ITEM_COUNT] = {
      tr(STR_GAME_RESUME), tr(STR_GOMOKU_UNDO), tr(STR_GOMOKU_RESIGN), tr(STR_GAME_NEW_GAME), tr(STR_GAME_EXIT),
  };
  gameMenu.show(tr(STR_GAME_GAME_MENU), options, MENU_ITEM_COUNT, 0,
                [this](const int index) { runMenuItem(static_cast<uint8_t>(index)); });
}

void ChineseChessGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();
}

void ChineseChessGameActivity::runMenuItem(uint8_t i) {
  switch (i) {
    case 0:  // Resume
      resumeFromMenu();
      return;
    case 1:  // Undo
      if (board.moveCount > 0) {
        board.undo();
        if (mode == ChineseChessMode::VsAi && board.moveCount > 0 && board.nextTurn() == kAiSide) {
          board.undo();
        }
        clearSelection();
        aiThinkingArmed = false;
        aiThinkingShown = false;
        scheduleSave();
      }
      resumeFromMenu();
      return;
    case 2: {  // Resign — current side to move resigns.
      const Side resigner = board.nextTurn();
      resignedFlag = true;
      resignWinner = (resigner == Side::Red) ? Side::Black : Side::Red;
      board.result =
          (resignWinner == Side::Red) ? ChineseChessBoard::Result::RedWins : ChineseChessBoard::Result::BlackWins;
      board.resigned = true;
      onGameOver();
      return;
    }
    case 3: {  // New Game
      const auto m = mode;
      const auto lv = aiLevel;
      ChineseChessStore::clear();
      activityManager.replaceActivityWith<ChineseChessGameActivity>(m, false, lv);
      return;
    }
    case 4:  // Exit to menu
      flushSave();
      activityManager.replaceActivityWith<ChineseChessMenuActivity>();
      return;
  }
}

void ChineseChessGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void ChineseChessGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  saveDebouncer.clear();
  ChineseChessSaveSlot slot;
  slot.board = board;
  slot.mode = mode;
  slot.aiLevel = aiLevel;
  slot.cursorR = cursorR;
  slot.cursorC = cursorC;
  slot.hasSelection = hasSelection;
  slot.selR = selR;
  slot.selC = selC;
  slot.redElapsedSec = static_cast<uint16_t>(redElapsedMs / 1000);
  slot.blackElapsedSec = static_cast<uint16_t>(blackElapsedMs / 1000);
  if (!ChineseChessStore::save(slot)) {
    LOG_ERR("XQI", "Failed to write save");
  }
}

// ---------- Render dispatch ----------

void ChineseChessGameActivity::render(RenderLock&&) {
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

void ChineseChessGameActivity::renderPlaying() {
  drawTitleBar();
  drawBoard();
  drawPieces();
  drawCursor();
  drawInfoPanel();
  drawModeLine();
  drawFooter();
}

// ---------- Title bar ----------

void ChineseChessGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H, w - 1, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  char left[64];
  if (mode == ChineseChessMode::VsAi) {
    snprintf(left, sizeof(left), "%s · %s [%s]", tr(STR_CHINESE_CHESS_TITLE), modeLabel(mode),
             difficultyShortLabel(aiLevel));
  } else {
    snprintf(left, sizeof(left), "%s · %s", tr(STR_CHINESE_CHESS_TITLE), modeLabel(mode));
  }
  renderer.drawText(kStatusFont, 12, y, left);

  // Right side: side-to-move label + move count. Per-side clock lives in the
  // info panel below; keeping the title bar uncluttered.
  Side marker = board.nextTurn();
  if (board.result == ChineseChessBoard::Result::RedWins) marker = Side::Red;
  if (board.result == ChineseChessBoard::Result::BlackWins) marker = Side::Black;

  char tail[32];
  snprintf(tail, sizeof(tail), "%s · %u", sideLabel(marker), static_cast<unsigned>(board.moveCount));
  const int rw = renderer.getTextWidth(kStatusFont, tail);
  renderer.drawText(kStatusFont, w - 12 - rw, y, tail);
}

// ---------- Board ----------

void ChineseChessGameActivity::drawBoard() {
  const int ox = boardOriginX();
  const int oy = BOARD_ORIGIN_Y;
  const int width = BOARD_PITCH * (ChineseChessBoard::FILES - 1);
  const int height = BOARD_PITCH * (ChineseChessBoard::RANKS - 1);

  // Outer frame (slightly inset).
  renderer.drawRect(ox - 4, oy - 4, width + 8, height + 8, 2, true);

  // Horizontal lines (10 ranks).
  for (int r = 0; r < ChineseChessBoard::RANKS; r++) {
    renderer.drawLine(ox, oy + r * BOARD_PITCH, ox + width, oy + r * BOARD_PITCH, true);
  }
  // Vertical lines: in Xiangqi the river splits all middle vertical lines
  // (ranks 4 and 5) — only files 0 and 8 span the full height.
  for (int c = 0; c < ChineseChessBoard::FILES; c++) {
    if (c == 0 || c == ChineseChessBoard::FILES - 1) {
      renderer.drawLine(ox + c * BOARD_PITCH, oy, ox + c * BOARD_PITCH, oy + height, true);
    } else {
      // Top half: ranks 0..4
      renderer.drawLine(ox + c * BOARD_PITCH, oy, ox + c * BOARD_PITCH, oy + 4 * BOARD_PITCH, true);
      // Bottom half: ranks 5..9
      renderer.drawLine(ox + c * BOARD_PITCH, oy + 5 * BOARD_PITCH, ox + c * BOARD_PITCH, oy + height, true);
    }
  }

  drawPalaceLines();
  drawRiver();
}

void ChineseChessGameActivity::drawPalaceLines() {
  const int ox = boardOriginX();
  const int oy = BOARD_ORIGIN_Y;
  // Black palace: rows 0..2, cols 3..5.
  {
    const int x0 = ox + 3 * BOARD_PITCH;
    const int y0 = oy + 0 * BOARD_PITCH;
    const int x1 = ox + 5 * BOARD_PITCH;
    const int y1 = oy + 2 * BOARD_PITCH;
    renderer.drawLine(x0, y0, x1, y1, true);
    renderer.drawLine(x1, y0, x0, y1, true);
  }
  // Red palace: rows 7..9, cols 3..5.
  {
    const int x0 = ox + 3 * BOARD_PITCH;
    const int y0 = oy + 7 * BOARD_PITCH;
    const int x1 = ox + 5 * BOARD_PITCH;
    const int y1 = oy + 9 * BOARD_PITCH;
    renderer.drawLine(x0, y0, x1, y1, true);
    renderer.drawLine(x1, y0, x0, y1, true);
  }
}

void ChineseChessGameActivity::drawRiver() {
  // The river sits between rows 4 and 5. Add two short horizontal lines and
  // a centered text label (using ASCII to avoid extra CJK glyph dependency).
  const int ox = boardOriginX();
  const int oy = BOARD_ORIGIN_Y;
  const int yMid = oy + 4 * BOARD_PITCH + BOARD_PITCH / 2;
  const int width = BOARD_PITCH * (ChineseChessBoard::FILES - 1);
  // Horizontal divider strokes — leave a gap in the middle for the label.
  const int gap = 130;
  renderer.drawLine(ox + 2, yMid, ox + width / 2 - gap / 2, yMid, true);
  renderer.drawLine(ox + width / 2 + gap / 2, yMid, ox + width - 2, yMid, true);
  const char* label = "River";
  const int tw = renderer.getTextWidth(kStatusFont, label);
  const int th = renderer.getTextHeight(kStatusFont);
  renderer.drawText(kStatusFont, ox + (width - tw) / 2, yMid - th / 2, label);
}

void ChineseChessGameActivity::drawPiece(int cx, int cy, uint8_t piece) const {
  if (piece == ChineseChessBoard::Empty) return;
  const Side s = ChineseChessBoard::sideOf(piece);
  const Kind k = ChineseChessBoard::kindOf(piece);
  const int r = PIECE_RADIUS;
  const int side2 = 2 * r + 1;

  // Erase background (board lines under the piece) so the glyph reads cleanly.
  // Black pieces get a light-gray fill so the two sides read distinctly on
  // monochrome E-ink; Red stays on white.
  const Color bgFill = (s == Side::Red) ? Color::White : Color::LightGray;
  renderer.fillRoundedRect(cx - r, cy - r, side2, side2, r, bgFill);
  // Outline.
  renderer.drawRoundedRect(cx - r, cy - r, side2, side2, 2, r, true);
  if (s == Side::Red) {
    // Red: thicker double-ring border to differentiate from Black on monochrome.
    renderer.drawRoundedRect(cx - r + 3, cy - r + 3, side2 - 6, side2 - 6, 1, r - 3, true);
  }

  const char* glyph = kPieceChar[static_cast<uint8_t>(s)][static_cast<uint8_t>(k)];
  const int tw = renderer.getTextWidth(CHINESE_CHESS_FONT_ID, glyph);
  const int th = renderer.getTextHeight(CHINESE_CHESS_FONT_ID);
  renderer.drawText(CHINESE_CHESS_FONT_ID, cx - tw / 2, cy - th / 2, glyph, true);
}

void ChineseChessGameActivity::drawPieces() {
  for (uint8_t r = 0; r < ChineseChessBoard::RANKS; r++) {
    for (uint8_t c = 0; c < ChineseChessBoard::FILES; c++) {
      const uint8_t v = board.cells[ChineseChessBoard::idx(r, c)];
      if (v == ChineseChessBoard::Empty) continue;
      int x, y;
      cellXY(r, c, &x, &y);
      drawPiece(x, y, v);
    }
  }

  // Last-move marker: small filled square at the destination of the last move.
  if (board.moveCount > 0) {
    const Move& last = board.moveHistory[board.moveCount - 1];
    int x, y;
    cellXY(ChineseChessBoard::rowOf(last.to), ChineseChessBoard::colOf(last.to), &x, &y);
    renderer.fillRect(x + PIECE_RADIUS - 6, y - PIECE_RADIUS, 5, 5, true);
  }

  // Selected piece outline + legal target dots.
  if (hasSelection) {
    int sx, sy;
    cellXY(selR, selC, &sx, &sy);
    const int r = PIECE_RADIUS + 3;
    renderer.drawRoundedRect(sx - r, sy - r, 2 * r + 1, 2 * r + 1, 2, r, true);
    for (uint8_t i = 0; i < legalCount; i++) {
      const uint8_t to = legalCache[i].to;
      int tx, ty;
      cellXY(ChineseChessBoard::rowOf(to), ChineseChessBoard::colOf(to), &tx, &ty);
      // Hollow dot 8x8 to mark legal target.
      renderer.fillRoundedRect(tx - 4, ty - 4, 9, 9, 4, Color::Black);
      renderer.fillRoundedRect(tx - 2, ty - 2, 5, 5, 2, Color::White);
    }
  }
}

void ChineseChessGameActivity::drawCursor() {
  if (state != State::Playing || board.isOver()) return;
  int cx, cy;
  cellXY(cursorR, cursorC, &cx, &cy);
  const int half = PIECE_RADIUS + 5;
  // Four corner brackets to form a focus indicator without obscuring the piece.
  const int armLen = 8;
  // Top-left
  renderer.drawLine(cx - half, cy - half, cx - half + armLen, cy - half, 2, true);
  renderer.drawLine(cx - half, cy - half, cx - half, cy - half + armLen, 2, true);
  // Top-right
  renderer.drawLine(cx + half - armLen, cy - half, cx + half, cy - half, 2, true);
  renderer.drawLine(cx + half, cy - half, cx + half, cy - half + armLen, 2, true);
  // Bottom-left
  renderer.drawLine(cx - half, cy + half, cx - half + armLen, cy + half, 2, true);
  renderer.drawLine(cx - half, cy + half - armLen, cx - half, cy + half, 2, true);
  // Bottom-right
  renderer.drawLine(cx + half - armLen, cy + half, cx + half, cy + half, 2, true);
  renderer.drawLine(cx + half, cy + half - armLen, cx + half, cy + half, 2, true);
}

// ---------- Info panel ----------

void ChineseChessGameActivity::drawInfoPanel() {
  const int sw = renderer.getScreenWidth();
  const int statY = INFO_PANEL_Y;
  constexpr int cellW = 180;
  constexpr int cellGap = 16;
  const int totalW = 2 * cellW + cellGap;
  const int statXStart = (sw - totalW) / 2;

  const bool gameLive = !board.isOver();
  const Side activeSide = gameLive ? board.nextTurn() : Side::Red;

  auto drawCell = [&](int xLeft, Side s) {
    renderer.drawRect(xLeft, statY, cellW, INFO_PANEL_H, true);
    // Per-side elapsed clock (chess-clock style: only the side to move ticks).
    char timeBuf[8];
    gameFormatElapsed((s == Side::Red) ? redElapsedMs : blackElapsedMs, timeBuf, sizeof(timeBuf));
    // King glyph as label: 帅 for Red, 将 for Black. Both live in CHINESE_CHESS_FONT_ID.
    const char* labelText = (s == Side::Red) ? "帅" : "将";
    const int labelH = renderer.getTextHeight(CHINESE_CHESS_FONT_ID);
    const int labelW = renderer.getTextWidth(CHINESE_CHESS_FONT_ID, labelText);
    const int valH = renderer.getTextHeight(kStatValueFont);
    const int valW = renderer.getTextWidth(kStatValueFont, timeBuf);
    constexpr int gap = 12;
    const int groupW = labelW + gap + valW;
    const int gx = xLeft + (cellW - groupW) / 2;
    const int cy = statY + INFO_PANEL_H / 2;
    renderer.drawText(CHINESE_CHESS_FONT_ID, gx, cy - labelH / 2, labelText, true);
    renderer.drawText(kStatValueFont, gx + labelW + gap, cy - valH / 2 - 2, timeBuf);

    if (gameLive && s == activeSide) {
      constexpr int triW = 8;
      constexpr int triH = 12;
      const int tx = xLeft + 10;
      const int ty = statY + (INFO_PANEL_H - triH) / 2;
      const int xs[3] = {tx, tx + triW, tx};
      const int ys[3] = {ty, ty + triH / 2, ty + triH};
      renderer.fillPolygon(xs, ys, 3, true);
    }
  };

  drawCell(statXStart, Side::Red);
  drawCell(statXStart + cellW + cellGap, Side::Black);
}

// ---------- Mode line ----------

void ChineseChessGameActivity::drawModeLine() {
  const char* label;
  char buf[64];
  if (mode == ChineseChessMode::VsAi && aiThinkingShown) {
    label = tr(STR_CHINESE_CHESS_AI_THINKING);
  } else if (hasSelection) {
    snprintf(buf, sizeof(buf), "%s · %s", tr(STR_CHINESE_CHESS_MOVE), tr(STR_CHINESE_CHESS_CANCEL));
    label = buf;
  } else {
    label = tr(STR_CHINESE_CHESS_SELECT);
  }

  const int contentTop = INFO_PANEL_Y + INFO_PANEL_H;
  const int contentBottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int y = gameCenteredBlockY(contentTop, contentBottom, renderer.getTextHeight(kStatusFont));
  renderer.drawCenteredText(kStatusFont, y, label);
}

void ChineseChessGameActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(
      hasSelection ? tr(STR_CHINESE_CHESS_CANCEL) : tr(STR_GAME_GAME_MENU_BTN),
      hasSelection ? tr(STR_CHINESE_CHESS_MOVE) : tr(STR_CHINESE_CHESS_SELECT_BTN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------- Game over screen ----------

void ChineseChessGameActivity::renderGameOver() {
  drawTitleBar();
  drawBoard();
  drawPieces();

  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  // Hero title.
  const char* heroText;
  switch (board.result) {
    case ChineseChessBoard::Result::RedWins:
      heroText = tr(STR_CHINESE_CHESS_RED_WINS);
      break;
    case ChineseChessBoard::Result::BlackWins:
      heroText = tr(STR_CHINESE_CHESS_BLACK_WINS);
      break;
    case ChineseChessBoard::Result::Draw:
      heroText = tr(STR_CHINESE_CHESS_DRAW);
      break;
    default:
      heroText = tr(STR_CHINESE_CHESS_DRAW);
      break;
  }
  const int heroW = renderer.getTextWidth(kHeroFont, heroText);
  const int heroH = renderer.getTextHeight(kHeroFont);

  // Banner over middle of board.
  const int bannerW = heroW + 80;
  const int bannerH = heroH + 32;
  const int bx = (sw - bannerW) / 2;
  const int by = (sh - bannerH) / 2;
  renderer.fillRect(bx, by, bannerW, bannerH, false);
  renderer.drawRect(bx, by, bannerW, bannerH, 2, true);
  renderer.drawText(kHeroFont, bx + (bannerW - heroW) / 2, by + (bannerH - heroH) / 2, heroText);

  // Footer: time + replay/exit hints.
  char timeStr[8];
  gameFormatElapsed(redElapsedMs + blackElapsedMs, timeStr, sizeof(timeStr));
  char tail[48];
  snprintf(tail, sizeof(tail), "%s %s · %u", tr(STR_GAME_TIME), timeStr, static_cast<unsigned>(board.moveCount));
  const int tw = renderer.getTextWidth(kStatusFont, tail);
  renderer.drawText(kStatusFont, (sw - tw) / 2, by + bannerH + 12, tail);

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

  const auto labels = mappedInput.mapLabels(tr(STR_GAME_HOME), tr(STR_GOMOKU_AGAIN), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
