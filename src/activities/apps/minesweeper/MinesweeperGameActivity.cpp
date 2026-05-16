#include "MinesweeperGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "MinesweeperGenerator.h"
#include "MinesweeperMenuActivity.h"
#include "MinesweeperStore.h"

namespace {

// Centralized font roles. All ≤16 pt so OMIT_LARGE_READER_FONTS builds work.
constexpr int kStatusFont = UI_12_FONT_ID;             // title bar
constexpr int kModalItemFont = UI_12_FONT_ID;          // game-menu modal rows
constexpr int kModalHintFont = UI_10_FONT_ID;          // game-menu modal right-side hints
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;        // end-screen big title
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;    // end-screen stat columns
constexpr int kNumberFontBig = NOTOSERIF_16_FONT_ID;   // cell digit (Easy/Medium)
constexpr int kNumberFontSmall = NOTOSANS_12_FONT_ID;  // cell digit (Hard)

}  // namespace

const char* MinesweeperGameActivity::difficultyName(MinesweeperBoard::Difficulty d) {
  switch (d) {
    case MinesweeperBoard::Difficulty::Medium:
      return tr(STR_MINESWEEPER_DIFFICULTY_MEDIUM);
    case MinesweeperBoard::Difficulty::Hard:
      return tr(STR_MINESWEEPER_DIFFICULTY_HARD);
    default:
      return tr(STR_MINESWEEPER_DIFFICULTY_EASY);
  }
}

MinesweeperGameActivity::MinesweeperGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 MinesweeperBoard::Difficulty difficulty, bool resume)
    : Activity("Minesweeper", renderer, mappedInput), difficulty(difficulty), resumeRequested(resume) {}

void MinesweeperGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  state = State::Playing;
  flagMode = false;
  hintsLeft = 3;
  elapsedMs = 0;
  lastTickMs = millis();
  saveDebouncer.clear();
  statsRecorded = false;
  board.init(difficulty);
  cursorR = static_cast<uint8_t>(board.rows / 2);
  cursorC = static_cast<uint8_t>(board.cols / 2);

  if (resumeRequested) {
    MinesweeperSaveSlot slot;
    if (MinesweeperStore::load(slot)) {
      board = slot.board;
      difficulty = slot.difficulty;
      cursorR = (slot.cursorR < board.rows) ? slot.cursorR : static_cast<uint8_t>(board.rows / 2);
      cursorC = (slot.cursorC < board.cols) ? slot.cursorC : static_cast<uint8_t>(board.cols / 2);
      flagMode = slot.flagMode;
      hintsLeft = slot.hintsLeft;
      elapsedMs = static_cast<uint32_t>(slot.elapsedSec) * 1000u;
      if (board.exploded) {
        state = State::Lost;
        statsRecorded = true;
      } else if (board.isWon()) {
        state = State::Won;
        statsRecorded = true;
      }
    } else {
      LOG_ERR("MSW", "Resume requested but save load failed");
      MinesweeperStore::clear();
      resumeRequested = false;
      MinesweeperStore::recordStart(difficulty);
    }
  } else {
    MinesweeperStore::recordStart(difficulty);
  }

  requestUpdate();
}

void MinesweeperGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void MinesweeperGameActivity::loop() {
  if (state == State::Playing) {
    const uint32_t now = millis();
    if (now > lastTickMs) {
      elapsedMs += now - lastTickMs;
    }
    lastTickMs = now;

    if (saveDebouncer.consumeIfDue(now)) {
      flushSave();
    }

    handleInputPlaying();
  } else if (state == State::GameMenu) {
    handleInputGameMenu();
  } else {
    handleInputEnd();
  }
}

void MinesweeperGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();  // reset timer base so paused interval doesn't count
}

void MinesweeperGameActivity::moveCursor(int dr, int dc) {
  const int nr = static_cast<int>(cursorR) + dr;
  const int nc = static_cast<int>(cursorC) + dc;
  if (nr < 0 || nr >= board.rows || nc < 0 || nc >= board.cols) return;
  cursorR = static_cast<uint8_t>(nr);
  cursorC = static_cast<uint8_t>(nc);
}

void MinesweeperGameActivity::doConfirmAction() {
  if (flagMode) {
    doFlag();
  } else {
    doDig();
  }
}

void MinesweeperGameActivity::doDig() {
  if (cursorR >= board.rows || cursorC >= board.cols) return;
  const auto& cell = board.at(cursorR, cursorC);
  if (cell.state == MinesweeperBoard::Revealed || cell.state == MinesweeperBoard::Flagged) return;
  if (!board.minesPlaced) {
    MinesweeperGenerator::placeMines(board, cursorR, cursorC);
  }
  const bool boom = board.dig(cursorR, cursorC);
  if (boom) {
    board.revealAllMines();
    onGameEnd(false);
  } else if (board.isWon()) {
    onGameEnd(true);
  } else {
    scheduleSave();
  }
}

void MinesweeperGameActivity::doFlag() {
  if (cursorR >= board.rows || cursorC >= board.cols) return;
  const auto& cell = board.at(cursorR, cursorC);
  if (cell.state == MinesweeperBoard::Revealed) return;
  board.toggleFlag(cursorR, cursorC);
  scheduleSave();
}

void MinesweeperGameActivity::onGameEnd(bool won) {
  state = won ? State::Won : State::Lost;
  if (!statsRecorded) {
    if (won) {
      MinesweeperStore::recordCompletion(difficulty, static_cast<uint16_t>(elapsedMs / 1000u));
    }
    statsRecorded = true;
  }
  MinesweeperStore::clear();  // game over — no point keeping in-progress save
  saveDebouncer.clear();
}

void MinesweeperGameActivity::useHint() {
  if (hintsLeft == 0) return;
  // Hint requires mines to be placed; if the user opens the menu before
  // their first dig, refuse the hint rather than commit to a random layout.
  if (!board.minesPlaced) return;
  // Collect all hidden non-mine cells.
  uint8_t candidates[MinesweeperBoard::MAX_CELLS];
  uint16_t n = 0;
  for (uint8_t r = 0; r < board.rows; r++) {
    for (uint8_t c = 0; c < board.cols; c++) {
      const auto& cell = board.at(r, c);
      if (cell.state == MinesweeperBoard::Hidden && !cell.hasMine) {
        candidates[n++] = static_cast<uint8_t>(r * board.cols + c);
      }
    }
  }
  if (n == 0) return;
  const uint16_t pick = static_cast<uint16_t>(esp_random() % n);
  const uint8_t idx = candidates[pick];
  const uint8_t r = idx / board.cols;
  const uint8_t c = idx % board.cols;
  // If the cell was previously flagged, unflag first so dig succeeds.
  if (board.at(r, c).state == MinesweeperBoard::Flagged) {
    board.toggleFlag(r, c);
  }
  board.dig(r, c);
  hintsLeft--;
  cursorR = r;
  cursorC = c;
  if (board.isWon()) {
    onGameEnd(true);
  } else {
    scheduleSave();
  }
}

void MinesweeperGameActivity::resetGameKeepLayout() {
  board.resetForRestart();
  hintsLeft = 3;
  elapsedMs = 0;
  cursorR = static_cast<uint8_t>(board.rows / 2);
  cursorC = static_cast<uint8_t>(board.cols / 2);
  flagMode = false;
  statsRecorded = false;
  // Mine layout is preserved (resetForRestart leaves hasMine intact). If no
  // mines were ever placed, the next dig will still trigger generation.
  scheduleSave();
}

void MinesweeperGameActivity::scheduleSave() {
  if (state == State::Won || state == State::Lost) return;
  saveDebouncer.schedule(millis());
}

void MinesweeperGameActivity::flushSave() {
  if (state == State::Won || state == State::Lost) return;
  if (!board.minesPlaced && board.revealedCount == 0 && board.flagsPlaced == 0) {
    // Nothing to save yet.
    saveDebouncer.clear();
    return;
  }
  MinesweeperSaveSlot slot;
  slot.board = board;
  slot.difficulty = difficulty;
  slot.elapsedSec = static_cast<uint16_t>(elapsedMs / 1000u);
  slot.cursorR = cursorR;
  slot.cursorC = cursorC;
  slot.flagMode = flagMode;
  slot.hintsLeft = hintsLeft;
  MinesweeperStore::save(slot);
  saveDebouncer.clear();
}

void MinesweeperGameActivity::enterGameMenu() {
  state = State::GameMenu;
  menuSel = 0;
}

void MinesweeperGameActivity::handleInputPlaying() {
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
    doConfirmAction();
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    enterGameMenu();
    requestUpdate();
  }
}

void MinesweeperGameActivity::handleInputGameMenu() {
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

void MinesweeperGameActivity::runMenuItem(uint8_t i) {
  switch (i) {
    case 0:  // Resume
      resumeFromMenu();
      return;
    case 1:  // Toggle mode
      flagMode = !flagMode;
      scheduleSave();
      resumeFromMenu();
      return;
    case 2: {  // Flag / Unflag here (one-shot)
      doFlag();
      resumeFromMenu();
      return;
    }
    case 3:  // Use hint
      useHint();
      // Don't auto-resume if the hint won the game.
      if (state == State::GameMenu) resumeFromMenu();
      return;
    case 4:  // Restart (same mine layout)
      resetGameKeepLayout();
      resumeFromMenu();
      return;
    case 5: {  // New Game (same difficulty, new layout)
      const auto diff = difficulty;
      MinesweeperStore::clear();
      activityManager.replaceActivity(std::make_unique<MinesweeperGameActivity>(renderer, mappedInput, diff, false));
      return;
    }
    case 6:  // Exit to Minesweeper menu
      flushSave();
      activityManager.replaceActivity(std::make_unique<MinesweeperMenuActivity>(renderer, mappedInput));
      return;
  }
}

void MinesweeperGameActivity::handleInputEnd() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto diff = difficulty;
    activityManager.replaceActivity(std::make_unique<MinesweeperGameActivity>(renderer, mappedInput, diff, false));
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivity(std::make_unique<MinesweeperMenuActivity>(renderer, mappedInput));
  }
}

void MinesweeperGameActivity::render(RenderLock&&) {
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
    case State::Won:
      renderEnd(true);
      break;
    case State::Lost:
      renderEnd(false);
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void MinesweeperGameActivity::renderPlaying() {
  drawTitleBar();
  drawBoard();
  drawFooter();
}

void MinesweeperGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H, w, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  char left[64];
  snprintf(left, sizeof(left), "%s · %s", tr(STR_MINESWEEPER_TITLE), difficultyName(difficulty));
  renderer.drawText(kStatusFont, 12, y, left);

  char timeStr[8];
  gameFormatElapsed(elapsedMs, timeStr, sizeof(timeStr));
  const uint16_t minesLeft = (board.mineCount > board.flagsPlaced) ? (board.mineCount - board.flagsPlaced) : 0;
  char right[40];
  snprintf(right, sizeof(right), "%s · %u %s", timeStr, static_cast<unsigned>(minesLeft), tr(STR_MINESWEEPER_MINES));
  const int rw = renderer.getTextWidth(kStatusFont, right);
  renderer.drawText(kStatusFont, w - 12 - rw, y, right);
}

void MinesweeperGameActivity::drawBoard() {
  const int size = cellSize();
  const int x0 = BOARD_X;
  const int y0 = BOARD_Y;

  // Cells. The cursor frame is drawn separately below so cell content
  // rendering doesn't need to know which cell is active.
  for (uint8_t r = 0; r < board.rows; r++) {
    for (uint8_t c = 0; c < board.cols; c++) {
      drawCellContent(x0 + c * size, y0 + r * size, size, board.at(r, c));
    }
  }

  // Grid lines drawn over content. Use fillRect for both axes (GfxRenderer
  // drawLine width-thickening is horizontal-only; see SudokuGameActivity).
  for (int i = 0; i <= board.rows; i++) {
    renderer.fillRect(x0, y0 + i * size, board.cols * size + 1, 1, true);
  }
  for (int i = 0; i <= board.cols; i++) {
    renderer.fillRect(x0 + i * size, y0, 1, board.rows * size + 1, true);
  }

  // Cursor outline: 2-px frame over the active cell. Drawn last so it sits
  // above the cell content and grid lines.
  if (state != State::Lost && state != State::Won) {
    const int cx = x0 + cursorC * size;
    const int cy = y0 + cursorR * size;
    renderer.drawRect(cx, cy, size + 1, size + 1, 2, true);
  }
}

void MinesweeperGameActivity::drawCellContent(int cellX, int cellY, int size,
                                              const MinesweeperBoard::Cell& cell) const {
  const int innerX = cellX + 1;
  const int innerY = cellY + 1;
  const int innerW = size - 1;
  const int innerH = size - 1;

  if (cell.state == MinesweeperBoard::Hidden) {
    // Subtle dither for unrevealed cells so the grid reads as territory.
    renderer.fillRectDither(innerX, innerY, innerW, innerH, Color::LightGray);
    return;
  }

  if (cell.state == MinesweeperBoard::Flagged) {
    renderer.fillRectDither(innerX, innerY, innerW, innerH, Color::LightGray);
    drawFlag(cellX, cellY, size);
    return;
  }

  // Revealed.
  if (cell.hasMine) {
    if (cell.mineExploded) {
      renderer.fillRect(innerX, innerY, innerW, innerH, true);  // black background
      drawMine(cellX, cellY, size, /*onDarkBg=*/true);
    } else {
      // Plain revealed mine (shown when the player has lost).
      drawMine(cellX, cellY, size, /*onDarkBg=*/false);
    }
    return;
  }

  if (cell.neighborMines == 0) {
    // Already cleared — leave blank (white).
    return;
  }

  drawNumber(cellX, cellY, size, cell.neighborMines);
}

void MinesweeperGameActivity::drawMine(int cellX, int cellY, int size, bool onDarkBg) const {
  const int cx = cellX + size / 2;
  const int cy = cellY + size / 2;
  const int r = size / 5;      // body half-side
  const int spoke = size / 3;  // arm length
  const bool ink = !onDarkBg;
  // Body: filled rounded rect approximates a disc at small sizes.
  renderer.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r, r, ink ? Color::Black : Color::White);
  // 4 cardinal spokes.
  renderer.drawLine(cx, cy - spoke, cx, cy + spoke, ink);
  renderer.drawLine(cx - spoke, cy, cx + spoke, cy, ink);
  // 4 diagonal spokes (a bit shorter so they don't stick beyond the body too far).
  const int d = (spoke * 7) / 10;
  renderer.drawLine(cx - d, cy - d, cx + d, cy + d, ink);
  renderer.drawLine(cx - d, cy + d, cx + d, cy - d, ink);
}

void MinesweeperGameActivity::drawFlag(int cellX, int cellY, int size) const {
  const int poleX = cellX + size * 4 / 10;
  const int top = cellY + size / 5;
  const int bot = cellY + size * 4 / 5;
  // 2-px pole.
  renderer.fillRect(poleX, top, 2, bot - top, true);
  // Triangle flag pointing right, approximated with horizontal lines so it
  // works on any GfxRenderer without a fillTriangle primitive.
  const int flagHeight = (bot - top) * 2 / 5;
  const int flagMaxW = size / 3;
  for (int i = 0; i < flagHeight; i++) {
    const int w = flagMaxW - (i * flagMaxW / flagHeight);
    if (w <= 0) break;
    renderer.drawLine(poleX + 2, top + i, poleX + 2 + w, top + i, true);
  }
}

void MinesweeperGameActivity::drawNumber(int cellX, int cellY, int size, uint8_t n) const {
  if (n == 0 || n > 9) return;
  const int fontId = (size >= 36) ? kNumberFontBig : kNumberFontSmall;
  const char buf[2] = {static_cast<char>('0' + n), 0};
  const int tw = renderer.getTextWidth(fontId, buf, EpdFontFamily::BOLD);
  const int th = renderer.getTextHeight(fontId);
  // getTextHeight reports the line box including descender, but digit glyphs
  // have none — geometric centering lands the glyph below the cell midline.
  // Nudging up by size/7 restores optical centering across all cell sizes.
  const int opticalUp = size / 7;
  renderer.drawText(fontId, cellX + (size - tw) / 2, cellY + gameCenterY(size, th) - opticalUp, buf, true,
                    EpdFontFamily::BOLD);
}

void MinesweeperGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* leftLabel = "";
  const char* rightLabel = "";

  if (state == State::Playing) {
    backLabel = tr(STR_GAME_GAME_MENU_BTN);
    confirmLabel = flagMode ? tr(STR_MINESWEEPER_MODE_FLAG) : tr(STR_MINESWEEPER_MODE_DIG);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_GAME_RESUME);
    confirmLabel = tr(STR_SELECT);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else {  // Won or Lost
    backLabel = tr(STR_BACK);
    confirmLabel = tr(STR_MINESWEEPER_PLAY_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MinesweeperGameActivity::renderEnd(bool won) {
  drawTitleBar();
  const int sw = renderer.getScreenWidth();

  // Show the final board so the player can inspect the layout.
  drawBoard();

  const char* hero = won ? tr(STR_MINESWEEPER_CLEARED) : tr(STR_MINESWEEPER_BOOM);
  renderer.drawCenteredText(kHeroFont, ENDGAME_HERO_Y, hero, true, EpdFontFamily::BOLD);

  // Three-column stat row.
  char timeBuf[8];
  gameFormatElapsed(elapsedMs, timeBuf, sizeof(timeBuf));

  const int diffIdx = static_cast<int>(difficulty);
  const MinesweeperStats stats = MinesweeperStore::loadStats();
  const int statsY = ENDGAME_STATS_Y;
  // Stack value (16pt) on top of label (12pt) with a generous gap; pad above
  // and below so the box doesn't crowd the hero text or footer hints.
  const int valTextH = renderer.getTextHeight(kStatValueFont);
  const int labTextH = renderer.getTextHeight(kStatusFont);
  constexpr int valueLabelGap = 14;
  constexpr int verticalPadding = 16;
  const int statsH = verticalPadding * 2 + valTextH + valueLabelGap + labTextH;
  const int valueY = statsY + verticalPadding;
  const int labelY = valueY + valTextH + valueLabelGap;
  const int sx = CONTENT_X;
  const int sw2 = sw - 2 * CONTENT_X;
  const int colW = sw2 / 3;

  renderer.drawLine(sx, statsY, sx + sw2, statsY, true);
  renderer.drawLine(sx, statsY + statsH, sx + sw2, statsY + statsH, true);

  auto drawStatCol = [&](int col, const char* label, const char* value) {
    const int cx = sx + col * colW;
    const int valW = renderer.getTextWidth(kStatValueFont, value);
    const int labW = renderer.getTextWidth(kStatusFont, label);
    renderer.drawText(kStatValueFont, cx + (colW - valW) / 2, valueY, value, true);
    renderer.drawText(kStatusFont, cx + (colW - labW) / 2, labelY, label, true);
  };

  drawStatCol(0, tr(STR_GAME_TIME), timeBuf);

  char bestBuf[16];
  if (diffIdx >= 0 && diffIdx < 3 && stats.bestTimeSec[diffIdx] > 0) {
    const uint16_t bs = stats.bestTimeSec[diffIdx];
    snprintf(bestBuf, sizeof(bestBuf), "%02u:%02u", static_cast<unsigned>(bs / 60), static_cast<unsigned>(bs % 60));
  } else {
    snprintf(bestBuf, sizeof(bestBuf), "--");
  }
  drawStatCol(1, tr(STR_GAME_BEST_TIME), bestBuf);

  char totalBuf[16];
  if (diffIdx >= 0 && diffIdx < 3) {
    snprintf(totalBuf, sizeof(totalBuf), "%u", static_cast<unsigned>(stats.completedCount[diffIdx]));
  } else {
    snprintf(totalBuf, sizeof(totalBuf), "0");
  }
  drawStatCol(2, tr(STR_MINESWEEPER_COMPLETED), totalBuf);

  drawFooter();
}

void MinesweeperGameActivity::renderGameMenu() {
  // Compact modal sized for English; MENU_ITEM_COUNT rows × rowH + title.
  constexpr int titleH = 28;
  constexpr int rowH = 32;
  const int w = 340;
  const int h = titleH + rowH * MENU_ITEM_COUNT + 4;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = (renderer.getScreenHeight() - h) / 2;

  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, 2, true);

  const int titleTextH = renderer.getTextHeight(kModalItemFont);
  renderer.fillRect(x + 2, y + titleH, w - 4, 1, true);
  renderer.drawText(kModalItemFont, x + 12, y + gameCenterY(titleH, titleTextH), tr(STR_GAME_GAME_MENU));

  // Item 2 label changes based on current cell state.
  const bool isFlagged =
      (cursorR < board.rows && cursorC < board.cols && board.at(cursorR, cursorC).state == MinesweeperBoard::Flagged);
  const char* flagItemLabel = isFlagged ? tr(STR_MINESWEEPER_UNFLAG_HERE) : tr(STR_MINESWEEPER_FLAG_HERE);

  const char* labels[MENU_ITEM_COUNT] = {
      tr(STR_GAME_RESUME),         tr(STR_MINESWEEPER_TOGGLE_MODE), flagItemLabel,     tr(STR_MINESWEEPER_USE_HINT),
      tr(STR_MINESWEEPER_RESTART), tr(STR_GAME_NEW_GAME),           tr(STR_GAME_EXIT),
  };

  // Right-side hints.
  const char* hintMode = flagMode ? tr(STR_MINESWEEPER_MODE_FLAG) : tr(STR_MINESWEEPER_MODE_DIG);
  char hintHint[16];
  if (hintsLeft > 0) {
    snprintf(hintHint, sizeof(hintHint), tr(STR_MINESWEEPER_HINTS_LEFT), static_cast<int>(hintsLeft));
  } else {
    snprintf(hintHint, sizeof(hintHint), "%s", tr(STR_MINESWEEPER_NO_HINTS));
  }
  const char* hints[MENU_ITEM_COUNT] = {"", hintMode, "", hintHint, "", "", tr(STR_GAME_HOME)};

  const int itemTextH = renderer.getTextHeight(kModalItemFont);
  const int hintTextH = renderer.getTextHeight(kModalHintFont);
  const int firstY = y + titleH;

  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    const int rowY = firstY + i * rowH;
    const bool inverted = (i == menuSel);
    if (inverted) {
      renderer.fillRect(x + 1, rowY, w - 2, rowH, true);
    }
    renderer.drawText(kModalItemFont, x + 12, rowY + gameCenterY(rowH, itemTextH), labels[i], !inverted);
    if (hints[i] && hints[i][0] != '\0') {
      const int hw = renderer.getTextWidth(kModalHintFont, hints[i]);
      renderer.drawText(kModalHintFont, x + w - 12 - hw, rowY + gameCenterY(rowH, hintTextH) + 2, hints[i], !inverted);
    }
  }
}
