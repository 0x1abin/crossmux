#include "SudokuGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "SudokuGenerator.h"
#include "SudokuStore.h"

namespace {

// Centralized font roles. Switch between sizes here when build flags change.
// `gh_release_zh` defines OMIT_LARGE_READER_FONTS, dropping the 18 pt fonts —
// keep all roles ≤ 16 pt so the activity works in every build environment.
constexpr int kBigDigitFont = NOTOSERIF_16_FONT_ID;   // fixed (given) digits + palette numerals
constexpr int kUserDigitFont = NOTOSERIF_14_FONT_ID;  // user-input digits — smaller = thinner & lighter
constexpr int kNotesFont = NOTOSANS_12_FONT_ID;      // 3×3 candidate notes
constexpr int kStatusFont = UI_12_FONT_ID;           // title bar / mode line
constexpr int kModalItemFont = UI_12_FONT_ID;        // game-menu modal rows
constexpr int kModalHintFont = UI_10_FONT_ID;        // game-menu right-side hints
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;      // win-screen "Solved!"
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;  // win-screen stat columns

// Vertically center text of height `textH` inside a box of height `boxH`.
// drawText's y is the TOP of the glyph (see GfxRenderer.cpp:249).
inline int centerY(int boxH, int textH) { return (boxH - textH) / 2; }

}  // namespace

SudokuGameActivity::SudokuGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       SudokuBoard::Difficulty difficulty, bool resume)
    : Activity("Sudoku", renderer, mappedInput), difficulty(difficulty), resumeRequested(resume) {}

void SudokuGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  // Default fresh-game state.
  state = State::Generating;
  generationDone = false;
  focus = Focus::Grid;
  cursorR = 4;
  cursorC = 4;
  paletteIdx = 0;
  lastPlacedDigit = 1;
  notesMode = false;
  mistakes = 0;
  hintsLeft = 3;
  elapsedMs = 0;
  lastTickMs = millis();
  pendingSaveAtMs = 0;

  // Resume overrides defaults if a save exists.
  if (resumeRequested) {
    SudokuSaveSlot slot;
    if (SudokuStore::load(slot)) {
      board = slot.board;
      difficulty = slot.difficulty;
      cursorR = slot.cursorR < 9 ? slot.cursorR : 4;
      cursorC = slot.cursorC < 9 ? slot.cursorC : 4;
      notesMode = slot.notesMode;
      mistakes = slot.mistakes;
      hintsLeft = slot.hintsLeft;
      elapsedMs = static_cast<uint32_t>(slot.elapsedSec) * 1000u;
      generationDone = true;
      state = State::Playing;
    } else {
      LOG_ERR("SDK", "Resume requested but save load failed");
    }
  }

  requestUpdate();
}

void SudokuGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void SudokuGameActivity::loop() {
  // Generation runs on the first loop tick after the loading frame is shown.
  if (state == State::Generating && !generationDone) {
    if (!SudokuGenerator::generate(board, difficulty)) {
      LOG_ERR("SDK", "Generation failed");
    }
    SudokuStore::recordStart(difficulty);
    generationDone = true;
    state = State::Playing;
    lastTickMs = millis();
    requestUpdate();
    return;
  }

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
  } else if (state == State::Won) {
    handleInputWon();
  }
}

void SudokuGameActivity::resumeFromMenu() {
  state = State::Playing;
  lastTickMs = millis();  // reset timer base so paused interval doesn't count
}

void SudokuGameActivity::handleInputWon() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto diff = difficulty;
    activityManager.replaceActivity(std::make_unique<SudokuGameActivity>(renderer, mappedInput, diff, false));
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void SudokuGameActivity::enterGameMenu() {
  state = State::GameMenu;
  menuSel = 0;
}

void SudokuGameActivity::handleInputGameMenu() {
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

void SudokuGameActivity::runMenuItem(uint8_t i) {
  switch (i) {
    case 0:  // Resume
      resumeFromMenu();
      return;
    case 1:  // Toggle notes mode
      notesMode = !notesMode;
      scheduleSave();
      resumeFromMenu();
      return;
    case 2:  // Use hint
      useHint();
      resumeFromMenu();
      return;
    case 3:  // Check errors (already realtime; closing menu is enough)
      resumeFromMenu();
      return;
    case 4:  // Restart current puzzle
      resetGame();
      resumeFromMenu();
      return;
    case 5: {  // New game (same difficulty)
      const auto diff = difficulty;
      SudokuStore::clear();
      activityManager.replaceActivity(std::make_unique<SudokuGameActivity>(renderer, mappedInput, diff, false));
      return;
    }
    case 6:  // Exit to home
      flushSave();
      activityManager.goHome();
      return;
  }
}

void SudokuGameActivity::useHint() {
  if (hintsLeft == 0) return;
  uint8_t candidates[81];
  int n = 0;
  for (int i = 0; i < 81; i++) {
    if (board.fixed[i] == 0 && board.user[i] == 0 && board.solution[i] != 0) {
      candidates[n++] = static_cast<uint8_t>(i);
    }
  }
  if (n == 0) return;
  const int pick = static_cast<int>(esp_random() % static_cast<uint32_t>(n));
  const uint8_t i = candidates[pick];
  board.user[i] = board.solution[i];
  board.notes[i] = 0;
  hintsLeft--;
  scheduleSave();
}

void SudokuGameActivity::resetGame() {
  board.resetUser();
  mistakes = 0;
  hintsLeft = 3;
  elapsedMs = 0;
  cursorR = 4;
  cursorC = 4;
  notesMode = false;
  focus = Focus::Grid;
  scheduleSave();
}

void SudokuGameActivity::handleInputPlaying() {
  if (focus == Focus::Grid) {
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
      // Confirm on a fixed cell does nothing; otherwise enter palette focus.
      if (!board.isFixed(cursorR, cursorC)) {
        enterPaletteFocus();
        requestUpdate();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      enterGameMenu();
      requestUpdate();
    }
  } else {
    // Palette focus
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      movePalette(-1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      movePalette(1, 0);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      movePalette(0, -1);
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      movePalette(0, 1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      exitPaletteFocus(true);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitPaletteFocus(false);
      requestUpdate();
    }
  }
}

void SudokuGameActivity::moveCursor(int dr, int dc) {
  cursorR = static_cast<uint8_t>((cursorR + dr + 9) % 9);
  cursorC = static_cast<uint8_t>((cursorC + dc + 9) % 9);
}

void SudokuGameActivity::movePalette(int dr, int dc) {
  // 5 cols × 2 rows: row mod 2, col mod 5.
  int row = paletteIdx / 5;
  int col = paletteIdx % 5;
  row = (row + dr + 2) % 2;
  col = (col + dc + 5) % 5;
  paletteIdx = static_cast<uint8_t>(row * 5 + col);
}

void SudokuGameActivity::enterPaletteFocus() {
  focus = Focus::Palette;
  // Pre-select erase if cell already has a user value (fast correction);
  // otherwise pre-select the last placed digit.
  if (!board.isEmpty(cursorR, cursorC) && !board.isFixed(cursorR, cursorC)) {
    paletteIdx = 9;
  } else {
    paletteIdx = static_cast<uint8_t>((lastPlacedDigit >= 1 && lastPlacedDigit <= 9) ? (lastPlacedDigit - 1) : 0);
  }
}

void SudokuGameActivity::exitPaletteFocus(bool place) {
  if (place) {
    const uint8_t d = (paletteIdx == 9) ? 0 : static_cast<uint8_t>(paletteIdx + 1);
    doPlace(d);
  }
  focus = Focus::Grid;
}

void SudokuGameActivity::doPlace(uint8_t digit) {
  if (board.isFixed(cursorR, cursorC)) return;

  if (notesMode) {
    if (digit >= 1 && digit <= 9) {
      board.toggleNote(cursorR, cursorC, digit);
    } else if (digit == 0) {
      board.user[SudokuBoard::idx(cursorR, cursorC)] = 0;
      board.notes[SudokuBoard::idx(cursorR, cursorC)] = 0;
    }
    scheduleSave();
    return;
  }

  // Normal mode: write digit (or erase). Erase doesn't count as a mistake.
  board.placeDigit(cursorR, cursorC, digit);
  if (digit != 0) {
    lastPlacedDigit = digit;
    const uint8_t expected = board.solution[SudokuBoard::idx(cursorR, cursorC)];
    if (expected != 0 && digit != expected && mistakes < 255) {
      mistakes++;
    }
  }
  scheduleSave();

  if (state == State::Playing && board.isSolved()) {
    onSolved();
  }
}

void SudokuGameActivity::onSolved() {
  const uint32_t totalSec = elapsedMs / 1000;
  const uint16_t cap = static_cast<uint16_t>(totalSec > 0xFFFF ? 0xFFFF : totalSec);
  SudokuStore::recordCompletion(difficulty, cap);
  SudokuStore::clear();
  pendingSaveAtMs = 0;
  state = State::Won;
}

void SudokuGameActivity::scheduleSave() { pendingSaveAtMs = millis() + 1500; }

void SudokuGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  if (!generationDone) return;
  pendingSaveAtMs = 0;
  SudokuSaveSlot slot;
  slot.board = board;
  slot.difficulty = difficulty;
  slot.elapsedSec = static_cast<uint16_t>(elapsedMs / 1000);
  slot.cursorR = cursorR;
  slot.cursorC = cursorC;
  slot.notesMode = notesMode;
  slot.mistakes = mistakes;
  slot.hintsLeft = hintsLeft;
  if (!SudokuStore::save(slot)) {
    LOG_ERR("SDK", "Failed to write save");
  }
}

bool SudokuGameActivity::cellHasError(uint8_t r, uint8_t c) const {
  const uint8_t i = SudokuBoard::idx(r, c);
  if (board.fixed[i] != 0) return false;
  if (board.user[i] == 0) return false;
  if (board.solution[i] == 0) return false;
  return board.user[i] != board.solution[i];
}

const char* SudokuGameActivity::difficultyName(SudokuBoard::Difficulty d) {
  switch (d) {
    case SudokuBoard::Difficulty::Medium:
      return tr(STR_SUDOKU_DIFFICULTY_MEDIUM);
    case SudokuBoard::Difficulty::Hard:
      return tr(STR_SUDOKU_DIFFICULTY_HARD);
    case SudokuBoard::Difficulty::Easy:
    default:
      return tr(STR_SUDOKU_DIFFICULTY_EASY);
  }
}

void SudokuGameActivity::render(RenderLock&&) {
  // Lock orientation each frame in case external code changed it.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  switch (state) {
    case State::Generating:
      renderGenerating();
      break;
    case State::Playing:
      renderPlaying();
      break;
    case State::GameMenu:
      renderPlaying();
      renderGameMenu();
      break;
    case State::Won:
      renderWon();
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SudokuGameActivity::renderGenerating() {
  drawTitleBar();
  const int h = renderer.getScreenHeight();
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, h / 2 - 16, tr(STR_SUDOKU_GENERATING));
  renderer.drawCenteredText(kStatusFont, h / 2 + 14, tr(STR_SUDOKU_GENERATING_HINT));
}

void SudokuGameActivity::renderPlaying() {
  drawTitleBar();
  drawGrid(GRID_X, GRID_Y);
  drawPalette(PALETTE_X, PALETTE_Y);
  drawModeLine();
  drawFooter();
}

void SudokuGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  // Bottom border at TITLE_BAR_H. 1px so plain drawLine is fine.
  renderer.drawLine(0, TITLE_BAR_H, w, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = centerY(TITLE_BAR_H, textH);

  char left[64];
  snprintf(left, sizeof(left), "%s · %s", tr(STR_SUDOKU_TITLE), difficultyName(difficulty));
  renderer.drawText(kStatusFont, 12, y, left);

  if (state != State::Generating) {
    char timeStr[8];
    formatTime(elapsedMs, timeStr, sizeof(timeStr));
    char right[32];
    snprintf(right, sizeof(right), "%s · %u/3", timeStr, static_cast<unsigned>(mistakes));
    const int rw = renderer.getTextWidth(kStatusFont, right);
    renderer.drawText(kStatusFont, w - 12 - rw, y, right);
  }
}

void SudokuGameActivity::drawGrid(int x0, int y0) {
  const int cell = CELL_PX;
  const int total = GRID_SIZE_PX;

  // 1. Peer cell background (cursor's row + col only).
  if (focus == Focus::Grid) {
    for (int i = 0; i < 9; i++) {
      if (i != cursorC) {
        renderer.fillRectDither(x0 + i * cell + 1, y0 + cursorR * cell + 1, cell - 2, cell - 2, Color::LightGray);
      }
      if (i != cursorR) {
        renderer.fillRectDither(x0 + cursorC * cell + 1, y0 + i * cell + 1, cell - 2, cell - 2, Color::LightGray);
      }
    }
  }

  // 2. Cursor cell: filled black if Grid focused, hollow border if Palette focused.
  if (focus == Focus::Grid) {
    renderer.fillRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, true);
  } else {
    renderer.drawRect(x0 + cursorC * cell, y0 + cursorR * cell, cell, cell, 2, true);
  }

  // 3. Grid lines drawn over highlights. Use fillRect because
  // GfxRenderer::drawLine(...lineWidth) only thickens horizontally
  // (lib bug at GfxRenderer.cpp:350); fillRect makes both axes match.
  for (int i = 0; i <= 9; i++) {
    const int lw = (i % 3 == 0) ? 2 : 1;
    renderer.fillRect(x0, y0 + i * cell, total + 1, lw, true);
    renderer.fillRect(x0 + i * cell, y0, lw, total + 1, true);
  }

  // 4. Same-value highlight when Palette focused.
  uint8_t selDigit = 0;
  if (focus == Focus::Palette && paletteIdx < 9) {
    selDigit = static_cast<uint8_t>(paletteIdx + 1);
  }

  // 5. Digits, notes, error marks.
  const int fixedDigitH = renderer.getTextHeight(kBigDigitFont);
  const int userDigitH = renderer.getTextHeight(kUserDigitFont);
  for (int r = 0; r < 9; r++) {
    for (int c = 0; c < 9; c++) {
      const uint8_t v = board.getValue(r, c);
      const bool isCursor = (focus == Focus::Grid) && (r == cursorR) && (c == cursorC);

      if (selDigit != 0 && v == selDigit && !isCursor) {
        renderer.drawRect(x0 + c * cell + 2, y0 + r * cell + 2, cell - 4, cell - 4, 2, true);
      }

      if (v != 0) {
        const bool isFixed = board.isFixed(r, c);
        const int fontId = isFixed ? kBigDigitFont : kUserDigitFont;
        const EpdFontFamily::Style style = isFixed ? EpdFontFamily::BOLD : EpdFontFamily::ITALIC;
        const int digitH = isFixed ? fixedDigitH : userDigitH;
        char buf[2] = {static_cast<char>('0' + v), 0};
        const int tw = renderer.getTextWidth(fontId, buf, style);
        const int x = x0 + c * cell + (cell - tw) / 2;
        const int y = y0 + r * cell + centerY(cell, digitH);
        renderer.drawText(fontId, x, y, buf, !isCursor, style);

        if (cellHasError(r, c)) {
          const int mx = x0 + c * cell + cell - 9;
          const int my = y0 + r * cell + cell - 8;
          renderer.drawLine(mx, my, mx + 5, my + 5, true);
          renderer.drawLine(mx + 5, my, mx, my + 5, true);
        }
      } else if (board.notes[SudokuBoard::idx(r, c)] != 0) {
        drawNotes(x0 + c * cell, y0 + r * cell, cell, board.notes[SudokuBoard::idx(r, c)]);
      }
    }
  }
}

void SudokuGameActivity::drawNotes(int cellX, int cellY, int cellSize, uint16_t notesBitmap) {
  const int sub = cellSize / 3;
  const int textH = renderer.getTextHeight(kNotesFont);
  for (int d = 1; d <= 9; d++) {
    if ((notesBitmap & (1u << d)) == 0) continue;
    const int k = d - 1;
    const int sx = cellX + (k % 3) * sub;
    const int sy = cellY + (k / 3) * sub;
    char buf[2] = {static_cast<char>('0' + d), 0};
    const int tw = renderer.getTextWidth(kNotesFont, buf);
    renderer.drawText(kNotesFont, sx + (sub - tw) / 2, sy + centerY(sub, textH), buf, true);
  }
}

void SudokuGameActivity::drawPalette(int x0, int y0) {
  if (focus == Focus::Palette) {
    renderer.drawRect(x0 - 4, y0 - 4, PALETTE_W + 8, PALETTE_H + 8, 2, true);
  }

  const int textH = renderer.getTextHeight(kBigDigitFont);
  for (int i = 0; i < 10; i++) {
    const int row = i / 5;
    const int col = i % 5;
    const int cx = x0 + col * (PALETTE_CELL_W + PALETTE_GAP);
    const int cy = y0 + row * (PALETTE_CELL_H + PALETTE_GAP);

    const bool isSelected = (focus == Focus::Palette) && (paletteIdx == i);
    if (isSelected) {
      renderer.fillRect(cx, cy, PALETTE_CELL_W, PALETTE_CELL_H, true);
    } else {
      renderer.drawRect(cx, cy, PALETTE_CELL_W, PALETTE_CELL_H, true);
    }

    const char ch = (i < 9) ? static_cast<char>('0' + (i + 1)) : 'X';
    const char buf[2] = {ch, 0};
    const int tw = renderer.getTextWidth(kBigDigitFont, buf);
    renderer.drawText(kBigDigitFont, cx + (PALETTE_CELL_W - tw) / 2, cy + centerY(PALETTE_CELL_H, textH), buf,
                      !isSelected);
  }
}

void SudokuGameActivity::drawModeLine() {
  const int y = MODE_LINE_Y;
  char left[40];
  snprintf(left, sizeof(left), "%s ×%u · %s %u/3", tr(STR_SUDOKU_USE_HINT), static_cast<unsigned>(hintsLeft),
           tr(STR_SUDOKU_ERRORS), static_cast<unsigned>(mistakes));
  renderer.drawText(kStatusFont, CONTENT_X, y, left);

  // Right-side "Notes" pill — only when notes mode is active.
  if (notesMode) {
    const char* tag = tr(STR_SUDOKU_MODE_NOTES);
    const int tw = renderer.getTextWidth(kStatusFont, tag);
    const int sw = renderer.getScreenWidth();
    constexpr int padX = 8;
    constexpr int boxH = 20;
    const int boxX = sw - CONTENT_X - tw - 2 * padX;
    renderer.drawRoundedRect(boxX, y - 14, tw + 2 * padX, boxH, 1, 8, true);
    renderer.drawText(kStatusFont, boxX + padX, y, tag);
  }
}

void SudokuGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* upLabel = "";
  const char* downLabel = "";

  if (state == State::Playing) {
    if (focus == Focus::Grid) {
      backLabel = tr(STR_SUDOKU_MENU);
      confirmLabel = tr(STR_SUDOKU_PICK_NUMBER);
    } else {
      backLabel = tr(STR_SUDOKU_CANCEL);
      confirmLabel = tr(STR_SUDOKU_PLACE);
    }
    upLabel = tr(STR_DIR_UP);
    downLabel = tr(STR_DIR_DOWN);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_SUDOKU_RESUME);
    confirmLabel = tr(STR_SELECT);
    upLabel = tr(STR_DIR_UP);
    downLabel = tr(STR_DIR_DOWN);
  } else if (state == State::Won) {
    backLabel = tr(STR_SUDOKU_HOME);
    confirmLabel = tr(STR_SUDOKU_PLAY_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, upLabel, downLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SudokuGameActivity::renderWon() {
  drawTitleBar();
  const int sw = renderer.getScreenWidth();

  // Big title
  const int titleY = 200;
  renderer.drawCenteredText(kHeroFont, titleY, tr(STR_SUDOKU_SOLVED), true, EpdFontFamily::BOLD);

  // Subtitle: difficulty · time · errors
  char timeBuf[8];
  formatTime(elapsedMs, timeBuf, sizeof(timeBuf));
  char sub[64];
  snprintf(sub, sizeof(sub), "%s · %s · %s %u/3", difficultyName(difficulty), timeBuf, tr(STR_SUDOKU_ERRORS),
           static_cast<unsigned>(mistakes));
  renderer.drawCenteredText(kStatusFont, titleY + 36, sub);

  // Three-column stats row.
  const int diffIdx = static_cast<int>(difficulty);
  const SudokuStats stats = SudokuStore::loadStats();
  const int statsY = titleY + 100;
  constexpr int statsH = 80;
  const int sx = CONTENT_X;
  const int sw2 = sw - 2 * CONTENT_X;
  const int colW = sw2 / 3;

  renderer.drawLine(sx, statsY, sx + sw2, statsY, true);
  renderer.drawLine(sx, statsY + statsH, sx + sw2, statsY + statsH, true);

  auto drawStatCol = [&](int col, const char* label, const char* value) {
    const int cx = sx + col * colW;
    const int valW = renderer.getTextWidth(kStatValueFont, value);
    const int labW = renderer.getTextWidth(kStatusFont, label);
    renderer.drawText(kStatValueFont, cx + (colW - valW) / 2, statsY + 28, value, true);
    renderer.drawText(kStatusFont, cx + (colW - labW) / 2, statsY + 56, label, true);
  };

  // Col 0: time.
  drawStatCol(0, tr(STR_SUDOKU_TIME), timeBuf);

  // Col 1: best.
  char bestBuf[16];
  if (diffIdx >= 0 && diffIdx < 3 && stats.bestTimeSec[diffIdx] > 0) {
    const uint16_t bs = stats.bestTimeSec[diffIdx];
    snprintf(bestBuf, sizeof(bestBuf), "%02u:%02u", static_cast<unsigned>(bs / 60), static_cast<unsigned>(bs % 60));
  } else {
    snprintf(bestBuf, sizeof(bestBuf), "--");
  }
  drawStatCol(1, tr(STR_SUDOKU_BEST_TIME), bestBuf);

  // Col 2: completed total.
  char totalBuf[16];
  if (diffIdx >= 0 && diffIdx < 3) {
    snprintf(totalBuf, sizeof(totalBuf), "%u", static_cast<unsigned>(stats.completedCount[diffIdx]));
  } else {
    snprintf(totalBuf, sizeof(totalBuf), "0");
  }
  drawStatCol(2, tr(STR_SUDOKU_COMPLETED), totalBuf);

  // Optional win-rate footnote.
  if (diffIdx >= 0 && diffIdx < 3 && stats.startedCount[diffIdx] > 0) {
    const int rate = (stats.completedCount[diffIdx] * 100) / stats.startedCount[diffIdx];
    char rateBuf[32];
    snprintf(rateBuf, sizeof(rateBuf), "%s %d%%", tr(STR_SUDOKU_WIN_RATE), rate);
    renderer.drawCenteredText(kStatusFont, statsY + statsH + 24, rateBuf);
  }

  drawFooter();
}

void SudokuGameActivity::renderGameMenu() {
  // Compact modal sized for English; 7 rows × rowH + title.
  constexpr int titleH = 28;
  constexpr int rowH = 32;
  const int w = 320;
  const int h = titleH + rowH * MENU_ITEM_COUNT + 4;
  const int x = (renderer.getScreenWidth() - w) / 2;
  const int y = (renderer.getScreenHeight() - h) / 2;

  renderer.fillRect(x, y, w, h, false);
  renderer.drawRect(x, y, w, h, 2, true);

  // Title bar.
  const int titleTextH = renderer.getTextHeight(kModalItemFont);
  renderer.fillRect(x + 2, y + titleH, w - 4, 1, true);
  renderer.drawText(kModalItemFont, x + 12, y + centerY(titleH, titleTextH), tr(STR_SUDOKU_GAME_MENU));

  const char* labels[MENU_ITEM_COUNT] = {
      tr(STR_SUDOKU_RESUME),  tr(STR_SUDOKU_TOGGLE_NOTES), tr(STR_SUDOKU_USE_HINT), tr(STR_SUDOKU_CHECK_ERRORS),
      tr(STR_SUDOKU_RESTART), tr(STR_SUDOKU_NEW_GAME),     tr(STR_SUDOKU_EXIT),
  };

  // Right-side hints (i18n-driven; no hardcoded English).
  const char* hintNotes = notesMode ? tr(STR_SUDOKU_MODE_NOTES) : tr(STR_SUDOKU_MODE_NORMAL);
  char hintHint[16];
  if (hintsLeft > 0) {
    snprintf(hintHint, sizeof(hintHint), tr(STR_SUDOKU_HINTS_LEFT), static_cast<int>(hintsLeft));
  } else {
    snprintf(hintHint, sizeof(hintHint), "%s", tr(STR_SUDOKU_NO_HINTS));
  }
  const char* hints[MENU_ITEM_COUNT] = {"", hintNotes, hintHint, "", "", "", tr(STR_SUDOKU_HOME)};

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

void SudokuGameActivity::formatTime(uint32_t ms, char* out, size_t outLen) const {
  const uint32_t totalSec = ms / 1000;
  const uint32_t mm = (totalSec / 60) % 100;
  const uint32_t ss = totalSec % 60;
  snprintf(out, outLen, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}
