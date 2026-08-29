#include "SudokuGameActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"
#include "SudokuGenerator.h"
#include "SudokuMenuActivity.h"
#include "SudokuStore.h"

namespace {

// Centralized font roles. Switch between sizes here when build flags change.
// `gh_release_zh` defines OMIT_LARGE_READER_FONTS, dropping the 18 pt fonts —
// keep all roles ≤ 16 pt so the activity works in every build environment.
constexpr int kBigDigitFont = NOTOSERIF_16_FONT_ID;   // fixed (given) digits + palette numerals
constexpr int kUserDigitFont = NOTOSERIF_14_FONT_ID;  // user-input digits — smaller = thinner & lighter
constexpr int kNotesFont = NOTOSANS_12_FONT_ID;       // 3×3 candidate notes
constexpr int kStatusFont = UI_12_FONT_ID;            // title bar / mode line
constexpr int kHeroFont = NOTOSERIF_16_FONT_ID;       // win-screen "Solved!"
constexpr int kStatValueFont = NOTOSANS_16_FONT_ID;   // win-screen stat columns

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
  saveDebouncer.clear();

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

    if (saveDebouncer.consumeIfDue(now)) {
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
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect again = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                         metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 0, 2);
  const Rect home = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                        metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 1, 2);
  if (mappedInput.wasTapInRect(again.x, again.y, again.width, again.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto diff = difficulty;
    activityManager.replaceActivityWith<SudokuGameActivity>(diff, false);
    return;
  }
  if (mappedInput.wasTapInRect(home.x, home.y, home.width, home.height) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.replaceActivityWith<SudokuMenuActivity>();
  }
}

void SudokuGameActivity::enterGameMenu() {
  state = State::GameMenu;
  const char* options[MENU_ITEM_COUNT] = {
      tr(STR_GAME_RESUME),    tr(STR_SUDOKU_TOGGLE_NOTES), tr(STR_SUDOKU_USE_HINT), tr(STR_SUDOKU_CHECK_ERRORS),
      tr(STR_SUDOKU_RESTART), tr(STR_GAME_NEW_GAME),       tr(STR_GAME_EXIT),
  };
  gameMenu.show(tr(STR_GAME_GAME_MENU), options, MENU_ITEM_COUNT, 0,
                [this](const int index) { runMenuItem(static_cast<uint8_t>(index)); });
}

void SudokuGameActivity::handleInputGameMenu() {
  gameMenu.handleInput(mappedInput, [this] { requestUpdate(); });
  if (!gameMenu.isActive() && state == State::GameMenu) resumeFromMenu();
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
      activityManager.replaceActivityWith<SudokuGameActivity>(diff, false);
      return;
    }
    case 6:  // Exit to Sudoku menu
      flushSave();
      activityManager.replaceActivityWith<SudokuMenuActivity>();
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
  int touchX = 0;
  int touchY = 0;
  const int gridX = (renderer.getScreenWidth() - GRID_SIZE_PX) / 2;
  const int paletteX = (renderer.getScreenWidth() - PALETTE_W) / 2;
  const auto updateTouchFocus = [&](const int x, const int y, const bool activate) {
    int row = 0;
    int column = 0;
    if (gameGridCellFromPoint(Rect{gridX, GRID_Y, GRID_SIZE_PX, GRID_SIZE_PX}, 9, 9, x, y, row, column)) {
      cursorR = static_cast<uint8_t>(row);
      cursorC = static_cast<uint8_t>(column);
      focus = Focus::Grid;
      if (activate && !board.isFixed(cursorR, cursorC)) enterPaletteFocus();
      requestUpdate();
      return true;
    }

    const int relativeX = x - paletteX;
    const int relativeY = y - PALETTE_Y;
    if (relativeX < 0 || relativeY < 0) return false;
    column = relativeX / (PALETTE_CELL_W + PALETTE_GAP);
    row = relativeY / (PALETTE_CELL_H + PALETTE_GAP);
    if (column >= 5 || row >= 2 || relativeX % (PALETTE_CELL_W + PALETTE_GAP) >= PALETTE_CELL_W ||
        relativeY % (PALETTE_CELL_H + PALETTE_GAP) >= PALETTE_CELL_H) {
      return false;
    }
    paletteIdx = static_cast<uint8_t>(row * 5 + column);
    focus = Focus::Palette;
    if (activate) exitPaletteFocus(true);
    requestUpdate();
    return true;
  };
  if (mappedInput.wasScreenTouchDown(touchX, touchY) && updateTouchFocus(touchX, touchY, false)) return;
  if (mappedInput.wasScreenTapped(touchX, touchY) && updateTouchFocus(touchX, touchY, true)) return;

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
  saveDebouncer.clear();
  state = State::Won;
}

void SudokuGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void SudokuGameActivity::flushSave() {
  if (state != State::Playing && state != State::GameMenu) return;
  if (!generationDone) return;
  saveDebouncer.clear();
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
      if (gameMenu.processRender(renderer, mappedInput)) return;
      break;
    case State::Won:
      renderWon();
      break;
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SudokuGameActivity::renderGenerating() {
  drawTitleBar();
  constexpr int gap = 12;
  const int titleH = renderer.getTextHeight(NOTOSANS_16_FONT_ID);
  const int hintH = renderer.getTextHeight(kStatusFont);
  const int titleY = gameCenteredBlockY(TITLE_BAR_H, renderer.getScreenHeight(), titleH + gap + hintH);
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, titleY, tr(STR_SUDOKU_GENERATING));
  renderer.drawCenteredText(kStatusFont, titleY + titleH + gap, tr(STR_SUDOKU_GENERATING_HINT));
}

void SudokuGameActivity::renderPlaying() {
  drawTitleBar();
  const int screenWidth = renderer.getScreenWidth();
  drawGrid((screenWidth - GRID_SIZE_PX) / 2, GRID_Y);
  drawPalette((screenWidth - PALETTE_W) / 2, PALETTE_Y);
  drawFooter();
}

void SudokuGameActivity::drawTitleBar() {
  const int w = renderer.getScreenWidth();
  // Bottom border at TITLE_BAR_H. 1px so plain drawLine is fine.
  renderer.drawLine(0, TITLE_BAR_H, w - 1, TITLE_BAR_H, true);

  const int textH = renderer.getTextHeight(kStatusFont);
  const int y = gameCenterY(TITLE_BAR_H, textH);

  char left[64];
  snprintf(left, sizeof(left), "%s · %s", tr(STR_SUDOKU_TITLE), difficultyName(difficulty));
  renderer.drawText(kStatusFont, 12, y, left);

  if (state != State::Generating) {
    char timeStr[8];
    gameFormatElapsed(elapsedMs, timeStr, sizeof(timeStr));
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
        const char buf[2] = {static_cast<char>('0' + v), 0};
        const int tw = renderer.getTextWidth(fontId, buf, style);
        const int x = x0 + c * cell + (cell - tw) / 2;
        const int y = y0 + r * cell + gameCenterY(cell, digitH);
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
    const char buf[2] = {static_cast<char>('0' + d), 0};
    const int tw = renderer.getTextWidth(kNotesFont, buf);
    renderer.drawText(kNotesFont, sx + (sub - tw) / 2, sy + gameCenterY(sub, textH), buf, true);
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
    renderer.drawText(kBigDigitFont, cx + (PALETTE_CELL_W - tw) / 2, cy + gameCenterY(PALETTE_CELL_H, textH), buf,
                      !isSelected);
  }
}

void SudokuGameActivity::drawFooter() {
  const char* backLabel = "";
  const char* confirmLabel = "";
  const char* leftLabel = "";
  const char* rightLabel = "";

  if (state == State::Playing) {
    if (focus == Focus::Grid) {
      backLabel = tr(STR_SUDOKU_MENU);
      confirmLabel = tr(STR_SUDOKU_PICK_NUMBER);
    } else {
      backLabel = tr(STR_SUDOKU_CANCEL);
      confirmLabel = tr(STR_SUDOKU_PLACE);
    }
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::GameMenu) {
    backLabel = tr(STR_GAME_RESUME);
    confirmLabel = tr(STR_SELECT);
    leftLabel = tr(STR_DIR_LEFT);
    rightLabel = tr(STR_DIR_RIGHT);
  } else if (state == State::Won) {
    backLabel = tr(STR_GAME_HOME);
    confirmLabel = tr(STR_SUDOKU_PLAY_AGAIN);
  }

  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, leftLabel, rightLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SudokuGameActivity::renderWon() {
  drawTitleBar();
  const int sw = renderer.getScreenWidth();

  // Subtitle: difficulty · time · errors
  char timeBuf[8];
  gameFormatElapsed(elapsedMs, timeBuf, sizeof(timeBuf));
  char sub[64];
  snprintf(sub, sizeof(sub), "%s · %s · %s %u/3", difficultyName(difficulty), timeBuf, tr(STR_SUDOKU_ERRORS),
           static_cast<unsigned>(mistakes));
  const int diffIdx = static_cast<int>(difficulty);
  const SudokuStats stats = SudokuStore::loadStats();

  char rateBuf[32];
  const bool showRate = diffIdx >= 0 && diffIdx < 3 && stats.startedCount[diffIdx] > 0;
  if (showRate) {
    const int rate = (stats.completedCount[diffIdx] * 100) / stats.startedCount[diffIdx];
    snprintf(rateBuf, sizeof(rateBuf), "%s %d%%", tr(STR_SUDOKU_WIN_RATE), rate);
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
  const int blockH = titleH + titleGap + statusH + sectionGap + statsH + (showRate ? footnoteGap + statusH : 0);
  const int availableBottom = renderer.getScreenHeight() - UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int titleY = gameCenteredBlockY(TITLE_BAR_H, availableBottom, blockH);
  const int subtitleY = titleY + titleH + titleGap;
  const int statsY = subtitleY + statusH + sectionGap;

  renderer.drawCenteredText(kHeroFont, titleY, tr(STR_SUDOKU_SOLVED), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(kStatusFont, subtitleY, sub);

  // Three-column stats row.
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

  // Col 0: time.
  drawStatCol(0, tr(STR_GAME_TIME), timeBuf);

  // Col 1: best.
  char bestBuf[16];
  if (diffIdx >= 0 && diffIdx < 3 && stats.bestTimeSec[diffIdx] > 0) {
    const uint16_t bs = stats.bestTimeSec[diffIdx];
    snprintf(bestBuf, sizeof(bestBuf), "%02u:%02u", static_cast<unsigned>(bs / 60), static_cast<unsigned>(bs % 60));
  } else {
    snprintf(bestBuf, sizeof(bestBuf), "--");
  }
  drawStatCol(1, tr(STR_GAME_BEST_TIME), bestBuf);

  // Col 2: completed total.
  char totalBuf[16];
  if (diffIdx >= 0 && diffIdx < 3) {
    snprintf(totalBuf, sizeof(totalBuf), "%u", static_cast<unsigned>(stats.completedCount[diffIdx]));
  } else {
    snprintf(totalBuf, sizeof(totalBuf), "0");
  }
  drawStatCol(2, tr(STR_SUDOKU_COMPLETED), totalBuf);

  if (showRate) {
    renderer.drawCenteredText(kStatusFont, statsY + statsH + footnoteGap, rateBuf);
  }

  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 0, 2),
        tr(STR_SUDOKU_PLAY_AGAIN));
    GUI.drawActionButton(
        renderer,
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 1, 2),
        tr(STR_GAME_HOME));
  }

  drawFooter();
}
