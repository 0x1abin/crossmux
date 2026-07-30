#include "SokobanGameActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"

SokobanGameActivity::SokobanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Sokoban", renderer, mappedInput), board(), levelOffsets{} {
  heldLevelSelectDir = 0;
  lastLevelSelectScrollTime = 0;
  isFirstLevelSelectHold = false;
}

void SokobanGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (!Storage.exists(SD_LEVEL_PATH)) {
    LOG_ERR("SOK", "SD card level file not found");
    activityManager.goToApps();
    return;
  }

  totalLevels = 0;
  HalFile f;
  if (!Storage.openFileForRead("SDK", SD_LEVEL_PATH, f)) {
    LOG_ERR("SOK", "Cannot open level file");
    activityManager.goToApps();
    return;
  }

  uint32_t offset = 0;
  while (totalLevels < MAX_LEVELS) {
    levelOffsets[totalLevels] = offset;
    uint8_t h, w;
    if (f.read(&h, 1) != 1 || f.read(&w, 1) != 1) break;
    uint32_t dataSize = w * h;
    offset += 2 + dataSize;
    f.seek(offset);
    totalLevels++;
  }
  f.close();

  if (totalLevels == 0) {
    LOG_ERR("SOK", "No levels found");
    activityManager.goToApps();
    return;
  }

  // 读取存档...
  SokobanSaveSlot slot;
  if (SokobanStore::load(slot)) {
    currentLevel = slot.currentLevel;
    if (currentLevel < 0 || currentLevel >= totalLevels) currentLevel = 0;
    moves = slot.moves;
  } else {
    currentLevel = 0;
    moves = 0;
  }
  loadLevel(currentLevel);
  requestUpdate();
}

void SokobanGameActivity::onExit() {
  flushSave();
  Activity::onExit();
}

void SokobanGameActivity::loadLevel(int idx) {
  if (idx < 0 || idx >= totalLevels) idx = 0;
  currentLevel = idx;

  HalFile f;
  if (!Storage.openFileForRead("SDK", SD_LEVEL_PATH, f)) {
    LOG_ERR("SOK", "Cannot open level file");
    activityManager.goToApps();
    return;
  }

  f.seek(levelOffsets[idx]);

  if (!board.loadFromFile(f)) {
    LOG_ERR("SOK", "Failed to load level %d", idx);
    f.close();
    activityManager.goToApps();
    return;
  }
  f.close();

  moves = 0;
  state = State::Playing;
  heldDr = heldDc = 0;
  heldLevelSelectDir = 0;
}

void SokobanGameActivity::loop() {
  const uint32_t now = millis();

  if (state == State::Won) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (currentLevel + 1 < totalLevels) {
        loadLevel(currentLevel + 1);
        scheduleSave();
      } else {
        activityManager.goToApps();
        return;
      }
      requestUpdate();
    }
    return;
  }

  if (state == State::LevelSelect) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (selectedLevel > 0) {
        selectedLevel--;
        if (selectedLevel < scrollOffset) scrollOffset--;
        requestUpdate();
      }
      heldLevelSelectDir = -1;
      lastLevelSelectScrollTime = now;
      isFirstLevelSelectHold = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (selectedLevel < totalLevels - 1) {
        selectedLevel++;
        if (selectedLevel >= scrollOffset + MENU_VISIBLE_ITEMS) scrollOffset++;
        requestUpdate();
      }
      heldLevelSelectDir = 1;
      lastLevelSelectScrollTime = now;
      isFirstLevelSelectHold = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      int newLevel = selectedLevel - 20;
      if (newLevel < 0) newLevel = 0;
      if (newLevel != selectedLevel) {
        selectedLevel = newLevel;
        if (selectedLevel < scrollOffset)
          scrollOffset = selectedLevel;
        else if (selectedLevel >= scrollOffset + MENU_VISIBLE_ITEMS)
          scrollOffset = selectedLevel - MENU_VISIBLE_ITEMS + 1;
        requestUpdate();
      }
      heldLevelSelectDir = -20;
      lastLevelSelectScrollTime = now;
      isFirstLevelSelectHold = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      int newLevel = selectedLevel + 20;
      if (newLevel >= totalLevels) newLevel = totalLevels - 1;
      if (newLevel != selectedLevel) {
        selectedLevel = newLevel;
        if (selectedLevel < scrollOffset)
          scrollOffset = selectedLevel;
        else if (selectedLevel >= scrollOffset + MENU_VISIBLE_ITEMS)
          scrollOffset = selectedLevel - MENU_VISIBLE_ITEMS + 1;
        requestUpdate();
      }
      heldLevelSelectDir = 20;
      lastLevelSelectScrollTime = now;
      isFirstLevelSelectHold = true;
    }

    if (heldLevelSelectDir != 0) {
      bool held = false;
      if (heldLevelSelectDir == -1 && mappedInput.isHeld(MappedInputManager::Button::Up))
        held = true;
      else if (heldLevelSelectDir == 1 && mappedInput.isHeld(MappedInputManager::Button::Down))
        held = true;
      else if (heldLevelSelectDir == -20 && mappedInput.isHeld(MappedInputManager::Button::Left))
        held = true;
      else if (heldLevelSelectDir == 20 && mappedInput.isHeld(MappedInputManager::Button::Right))
        held = true;

      if (held) {
        uint32_t delay = isFirstLevelSelectHold ? 350 : 180;
        if (now - lastLevelSelectScrollTime >= delay) {
          int step = abs(heldLevelSelectDir);
          int newLevel = selectedLevel + (heldLevelSelectDir > 0 ? step : -step);
          if (newLevel < 0) newLevel = 0;
          if (newLevel >= totalLevels) newLevel = totalLevels - 1;
          if (newLevel != selectedLevel) {
            selectedLevel = newLevel;
            if (selectedLevel < scrollOffset)
              scrollOffset = selectedLevel;
            else if (selectedLevel >= scrollOffset + MENU_VISIBLE_ITEMS)
              scrollOffset = selectedLevel - MENU_VISIBLE_ITEMS + 1;
            requestUpdate();
          }
          lastLevelSelectScrollTime = now;
          isFirstLevelSelectHold = false;
        }
      } else {
        heldLevelSelectDir = 0;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      loadLevel(selectedLevel);
      scheduleSave();
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::Playing;
      requestUpdate();
    }
    return;
  }

  handleInput();
  if (saveDebouncer.consumeIfDue(millis())) {
    flushSave();
  }
}

void SokobanGameActivity::handleInput() {
  const uint32_t now = millis();

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    move(-1, 0);
    heldDr = -1;
    heldDc = 0;
    lastCursorMoveTime = now;
    isFirstMoveAfterHold = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    move(1, 0);
    heldDr = 1;
    heldDc = 0;
    lastCursorMoveTime = now;
    isFirstMoveAfterHold = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    move(0, -1);
    heldDr = 0;
    heldDc = -1;
    lastCursorMoveTime = now;
    isFirstMoveAfterHold = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    move(0, 1);
    heldDr = 0;
    heldDc = 1;
    lastCursorMoveTime = now;
    isFirstMoveAfterHold = true;
  }

  if (heldDr != 0 || heldDc != 0) {
    bool held = false;
    if (heldDr == -1 && mappedInput.isHeld(MappedInputManager::Button::Up))
      held = true;
    else if (heldDr == 1 && mappedInput.isHeld(MappedInputManager::Button::Down))
      held = true;
    else if (heldDc == -1 && mappedInput.isHeld(MappedInputManager::Button::Left))
      held = true;
    else if (heldDc == 1 && mappedInput.isHeld(MappedInputManager::Button::Right))
      held = true;

    if (held) {
      uint32_t delay = isFirstMoveAfterHold ? kInitialHoldDelayMs : kRepeatMoveIntervalMs;
      if (now - lastCursorMoveTime >= delay) {
        move(heldDr, heldDc);
        lastCursorMoveTime = now;
        isFirstMoveAfterHold = false;
      }
    } else {
      heldDr = heldDc = 0;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    undo();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectedLevel = currentLevel;
    scrollOffset = (selectedLevel / MENU_VISIBLE_ITEMS) * MENU_VISIBLE_ITEMS;
    state = State::LevelSelect;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    flushSave();
    activityManager.goToApps();
    return;
  }
}

void SokobanGameActivity::move(int dr, int dc) {
  if (state != State::Playing) return;
  if (board.movePlayer(dr, dc)) {
    moves++;
    scheduleSave();
    requestUpdate();
    if (board.isWin()) {
      onWin();
    }
  }
}

void SokobanGameActivity::undo() {
  if (state != State::Playing) return;
  if (board.canUndo()) {
    board.undo();
    if (moves > 0) moves--;
    requestUpdate();
  }
}

void SokobanGameActivity::resetLevel() {
  loadLevel(currentLevel);
  requestUpdate();
}

void SokobanGameActivity::onWin() {
  state = State::Won;
  if (currentLevel + 1 < totalLevels) {
    SokobanStore::saveLevel(currentLevel + 1, 0);
  }
  requestUpdate();
}

void SokobanGameActivity::scheduleSave() { saveDebouncer.schedule(millis()); }

void SokobanGameActivity::flushSave() {
  if (state != State::Playing) return;
  SokobanSaveSlot slot;
  slot.currentLevel = currentLevel;
  slot.moves = moves;
  slot.hasBoard = false;
  SokobanStore::save(slot);
}

void SokobanGameActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  if (state == State::Won) {
    drawWinScreen();
  } else if (state == State::LevelSelect) {
    drawLevelSelect();
  } else {
    drawHUD();
    drawBoard();
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SokobanGameActivity::drawHUD() {
  const int sw = renderer.getScreenWidth();
  renderer.drawLine(0, TITLE_BAR_H + 8, sw, TITLE_BAR_H + 8, true);

  char buf[64];
  snprintf(buf, sizeof(buf), "%s %d/%d", tr(STR_SOKOBAN_TITLE), currentLevel + 1, totalLevels);
  renderer.drawText(UI_12_FONT_ID, 12, 8, buf);

  snprintf(buf, sizeof(buf), "%s:%d %s:%d", tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES), board.pushes);
  int tw = renderer.getTextWidth(UI_12_FONT_ID, buf);
  renderer.drawText(UI_12_FONT_ID, sw - 12 - tw, 8, buf);
}

void SokobanGameActivity::drawBoard() {
  if (board.rows == 0 || board.cols == 0) {
    LOG_ERR("SOK", "Empty board, skip drawing");
    return;
  }

  int availH = BOARD_BOTTOM - BOARD_TOP;
  int availW = renderer.getScreenWidth() - 16;
  int cellSize = 48;
  int maxCellByRows = availH / board.rows;
  int maxCellByCols = availW / board.cols;
  cellSize = (maxCellByRows < maxCellByCols) ? maxCellByRows : maxCellByCols;
  if (cellSize > 48) cellSize = 48;
  if (cellSize < 8) cellSize = 8;

  int boardPixelW = board.cols * cellSize;
  int boardPixelH = board.rows * cellSize;
  int startX = (renderer.getScreenWidth() - boardPixelW) / 2;
  int startY = BOARD_TOP + (availH - boardPixelH) / 2;

  for (int r = 0; r < board.rows; ++r) {
    for (int c = 0; c < board.cols; ++c) {
      int x = startX + c * cellSize;
      int y = startY + r * cellSize;
      SokobanBoard::Cell cell = board.cells[r][c];

      switch (cell) {
        case SokobanBoard::WALL:
          renderer.drawRect(x, y, cellSize, cellSize, 2, true);
          {
            int dotSize = (cellSize >= 16) ? 2 : 1;
            int margin = cellSize / 4;
            renderer.fillRect(x + margin, y + margin, dotSize, dotSize, true);
            renderer.fillRect(x + cellSize - margin - dotSize, y + margin, dotSize, dotSize, true);
            renderer.fillRect(x + margin, y + cellSize - margin - dotSize, dotSize, dotSize, true);
            renderer.fillRect(x + cellSize - margin - dotSize, y + cellSize - margin - dotSize, dotSize, dotSize, true);
          }
          break;
        case SokobanBoard::FLOOR:
          break;
        case SokobanBoard::TARGET:
          renderer.drawRect(x + cellSize / 4, y + cellSize / 4, cellSize / 2, cellSize / 2, 2, true);
          break;
        case SokobanBoard::BOX:
          renderer.drawRect(x + 2, y + 2, cellSize - 4, cellSize - 4, 3, true);
          renderer.drawLine(x + 2, y + 2, x + cellSize - 4, y + cellSize - 4, true);
          renderer.drawLine(x + cellSize - 4, y + 2, x + 2, y + cellSize - 4, true);
          break;
        case SokobanBoard::BOX_ON_TARGET:
          renderer.fillRect(x + 2, y + 2, cellSize - 4, cellSize - 4, true);
          renderer.drawRect(x + cellSize / 4, y + cellSize / 4, cellSize / 2, cellSize / 2, 2, false);
          break;
        case SokobanBoard::PLAYER:
          renderer.fillRect(x + 4, y + 4, cellSize - 8, cellSize - 8, true);
          break;
        case SokobanBoard::PLAYER_ON_TARGET:
          renderer.fillRect(x + 4, y + 4, cellSize - 8, cellSize - 8, true);
          renderer.drawRect(x + cellSize / 4, y + cellSize / 4, cellSize / 2, cellSize / 2, 2, false);
          break;
      }
    }
  }
}

void SokobanGameActivity::drawWinScreen() {
  const int sh = renderer.getScreenHeight();
  renderer.drawCenteredText(NOTOSERIF_14_FONT_ID, sh / 2 - 30, tr(STR_SOKOBAN_WIN));
  char buf[64];
  snprintf(buf, sizeof(buf), "%s %d %s %d", tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES), board.pushes);
  renderer.drawCenteredText(UI_12_FONT_ID, sh / 2 + 10, buf);
  renderer.drawCenteredText(UI_12_FONT_ID, sh / 2 + 35, tr(STR_SOKOBAN_WIN_HINT));
}

void SokobanGameActivity::drawLevelSelect() {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int itemH = 30;
  const int listTop = 36;
  const int listBottom = sh - 62;
  const int visibleCount = (listBottom - listTop) / itemH;
  if (visibleCount <= 0) return;

  char buf[64];
  snprintf(buf, sizeof(buf), "%s (%d)", tr(STR_SOKOBAN_TITLE), totalLevels);
  renderer.drawCenteredText(UI_12_FONT_ID, 8, buf);

  renderer.drawLine(0, 35, sw, 35, true);

  for (int i = 0; i < visibleCount && (scrollOffset + i) < totalLevels; ++i) {
    int levelIdx = scrollOffset + i;
    int y = listTop + i * itemH;

    if (levelIdx == selectedLevel) {
      renderer.fillRect(10, y, 4, itemH - 2, true);
    }

    snprintf(buf, sizeof(buf), "%s %d", tr(STR_SOKOBAN_LEVEL), levelIdx + 1);
    renderer.drawText(UI_12_FONT_ID, 22, y + 4, buf);
  }

  renderer.drawLine(0, sh - 49, sw, sh - 49, true);
  renderer.drawCenteredText(UI_10_FONT_ID, sh - 35, tr(STR_SOKOBAN_SELECT_HINT));
}