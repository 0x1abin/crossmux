#include "SokobanGameActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../GameUi.h"

namespace {
constexpr int kMaxCellSize = 48;
constexpr int kMinCellSize = 8;

struct BoardLayout {
  int cellSize = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  constexpr bool valid() const { return cellSize >= kMinCellSize; }
};

constexpr BoardLayout fitBoard(const int left, const int top, const int right, const int bottom, const int rows,
                               const int cols) {
  if (rows <= 0 || cols <= 0 || right <= left || bottom <= top) return {};

  int cellSize = (right - left) / cols;
  const int rowCellSize = (bottom - top) / rows;
  if (rowCellSize < cellSize) cellSize = rowCellSize;
  if (cellSize > kMaxCellSize) cellSize = kMaxCellSize;
  if (cellSize < kMinCellSize) return {};

  const int width = cols * cellSize;
  const int height = rows * cellSize;
  return {cellSize, left + (right - left - width) / 2, top + (bottom - top - height) / 2, width, height};
}

// X4 portrait with the tallest themed header and the widest built-in level (30x17).
constexpr BoardLayout kX4MaxLevelLayout = fitBoard(20, 145, 460, 744, 17, 30);
static_assert(kX4MaxLevelLayout.valid());
static_assert(kX4MaxLevelLayout.x >= 0 && kX4MaxLevelLayout.x + kX4MaxLevelLayout.width <= 480);
static_assert(kX4MaxLevelLayout.y >= 0 && kX4MaxLevelLayout.y + kX4MaxLevelLayout.height <= 800);

struct LevelSelectLayout {
  Rect content;
  int rowStep;
  int visibleCount;
};

Rect getContentArea(const GfxRenderer& renderer, const bool hasSubHeader, const bool hasTouch) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  int viewTop, viewRight, viewBottom, viewLeft;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);

  const int left = safeArea.x + std::max(metrics.contentSidePadding, viewLeft);
  const int top = safeArea.y + std::max(metrics.topPadding + metrics.headerHeight +
                                            (hasSubHeader ? metrics.tabBarHeight : 0) + metrics.verticalSpacing,
                                        viewTop);
  const int right = safeArea.x + safeArea.width - std::max(metrics.contentSidePadding, viewRight);
  const int touchActions = hasTouch ? metrics.menuRowHeight + renderer.getLineHeight(UI_10_FONT_ID) +
                                          metrics.verticalSpacing + metrics.menuSpacing
                                    : 0;
  const int bottom = safeArea.y + safeArea.height - std::max(metrics.verticalSpacing, viewBottom) - touchActions;
  return Rect{left, top, std::max(0, right - left), std::max(0, bottom - top)};
}

LevelSelectLayout getLevelSelectLayout(const GfxRenderer& renderer, const bool hasTouch) {
  const Rect content = getContentArea(renderer, false, hasTouch);
  const int rowStep = std::max(1, GUI.getListRowStep(false));
  return {content, rowStep, GUI.getListPageItems(content.height, false)};
}
}  // namespace

SokobanGameActivity::SokobanGameActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Sokoban", renderer, mappedInput), board(), levelOffsets{} {
  heldLevelSelectDir = 0;
  lastLevelSelectScrollTime = 0;
  isFirstLevelSelectHold = false;
}

void SokobanGameActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  totalLevels = 0;
  size_t offset = 0;
  while (offset + 2 <= builtinLevelsSize && totalLevels < MAX_LEVELS) {
    levelOffsets[totalLevels] = offset;
    uint8_t h = builtinLevels[offset];
    uint8_t w = builtinLevels[offset + 1];
    uint32_t dataSize = w * h;
    offset += 2 + dataSize;
    if (offset > builtinLevelsSize) break;
    totalLevels++;
  }

  if (totalLevels == 0) {
    LOG_ERR("SOK", "No levels found in builtin data");
    activityManager.goToApps();
    return;
  }

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

  uint32_t offset = levelOffsets[idx];
  if (offset + 2 > builtinLevelsSize) {
    LOG_ERR("SOK", "Level offset out of bounds");
    activityManager.goToApps();
    return;
  }

  uint8_t h = builtinLevels[offset];
  uint8_t w = builtinLevels[offset + 1];
  if (h == 0 || w == 0 || h > SokobanBoard::MAX_ROWS || w > SokobanBoard::MAX_COLS) {
    LOG_ERR("SOK", "Invalid level dimensions");
    activityManager.goToApps();
    return;
  }

  board.clear();
  board.rows = h;
  board.cols = w;
  const uint8_t* cellData = builtinLevels + offset + 2;
  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < w; ++c) {
      board.cells[r][c] = static_cast<SokobanBoard::Cell>(cellData[r * w + c]);
      if (board.cells[r][c] == SokobanBoard::PLAYER || board.cells[r][c] == SokobanBoard::PLAYER_ON_TARGET) {
        board.playerR = r;
        board.playerC = c;
      }
    }
  }

  moves = 0;
  state = State::Playing;
  heldDr = heldDc = 0;
  heldLevelSelectDir = 0;
}

void SokobanGameActivity::loop() {
  const uint32_t now = millis();

  if (state == State::Won) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect next = gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                          metrics.contentSidePadding, metrics.menuSpacing, metrics.menuRowHeight, 0, 1);
    if (mappedInput.wasTapInRect(next.x, next.y, next.width, next.height)) {
      if (currentLevel + 1 < totalLevels) {
        loadLevel(currentLevel + 1);
        scheduleSave();
        requestUpdate();
      } else {
        activityManager.goToApps();
      }
      return;
    }
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
    const LevelSelectLayout layout = getLevelSelectLayout(renderer, mappedInput.hasTouch());
    const int visibleCount = layout.visibleCount;
    int touched = -1;
    const auto touch = mappedInput.rowTouch(touched, layout.content.y, layout.rowStep,
                                            std::min(visibleCount, totalLevels - scrollOffset), layout.content.x,
                                            layout.content.x + layout.content.width, layout.rowStep);
    if (touch != MappedInputManager::RowTouch::None) {
      selectedLevel = scrollOffset + touched;
      requestUpdate();
      if (touch == MappedInputManager::RowTouch::Tap) {
        loadLevel(selectedLevel);
        scheduleSave();
      }
      return;
    }

    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect select =
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 0, 1);
    if (mappedInput.wasTapInRect(select.x, select.y, select.width, select.height)) {
      loadLevel(selectedLevel);
      scheduleSave();
      requestUpdate();
      return;
    }

    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up && selectedLevel < totalLevels - 1) {
      selectedLevel++;
      if (selectedLevel >= scrollOffset + visibleCount) scrollOffset++;
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down && selectedLevel > 0) {
      selectedLevel--;
      if (selectedLevel < scrollOffset) scrollOffset--;
      requestUpdate();
      return;
    }

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
        if (selectedLevel >= scrollOffset + visibleCount) scrollOffset++;
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
        else if (selectedLevel >= scrollOffset + visibleCount)
          scrollOffset = selectedLevel - visibleCount + 1;
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
        else if (selectedLevel >= scrollOffset + visibleCount)
          scrollOffset = selectedLevel - visibleCount + 1;
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
            else if (selectedLevel >= scrollOffset + visibleCount)
              scrollOffset = selectedLevel - visibleCount + 1;
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

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect undoButton =
      gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                          metrics.menuSpacing, metrics.menuRowHeight, 0, 2);
  const Rect levelButton =
      gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                          metrics.menuSpacing, metrics.menuRowHeight, 1, 2);
  if (mappedInput.wasTapInRect(undoButton.x, undoButton.y, undoButton.width, undoButton.height)) {
    undo();
    return;
  }
  if (mappedInput.wasTapInRect(levelButton.x, levelButton.y, levelButton.width, levelButton.height)) {
    selectedLevel = currentLevel;
    const int visibleCount = getLevelSelectLayout(renderer, mappedInput.hasTouch()).visibleCount;
    scrollOffset = (selectedLevel / visibleCount) * visibleCount;
    state = State::LevelSelect;
    requestUpdate();
    return;
  }

  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      move(-1, 0);
      return;
    case MappedInputManager::SwipeDir::Down:
      move(1, 0);
      return;
    case MappedInputManager::SwipeDir::Left:
      move(0, -1);
      return;
    case MappedInputManager::SwipeDir::Right:
      move(0, 1);
      return;
    case MappedInputManager::SwipeDir::None:
      break;
  }

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
    const int visibleCount = getLevelSelectLayout(renderer, mappedInput.hasTouch()).visibleCount;
    scrollOffset = (selectedLevel / visibleCount) * visibleCount;
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

  if (mappedInput.hasTouch()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect firstAction =
        gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                            metrics.menuSpacing, metrics.menuRowHeight, 0, state == State::Playing ? 2 : 1);
    renderer.drawCenteredText(UI_10_FONT_ID,
                              firstAction.y - renderer.getLineHeight(UI_10_FONT_ID) - metrics.menuSpacing,
                              tr(STR_SOKOBAN_TOUCH_HINT));
    if (state == State::Playing) {
      GUI.drawActionButton(
          renderer,
          gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                              metrics.menuSpacing, metrics.menuRowHeight, 0, 2),
          tr(STR_GOMOKU_UNDO));
      GUI.drawActionButton(
          renderer,
          gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                              metrics.menuSpacing, metrics.menuRowHeight, 1, 2),
          tr(STR_SOKOBAN_LEVEL));
    } else {
      GUI.drawActionButton(
          renderer,
          gameTouchActionRect(renderer.getScreenWidth(), renderer.getScreenHeight(), metrics.contentSidePadding,
                              metrics.menuSpacing, metrics.menuRowHeight, 0, 1),
          tr(STR_SELECT));
    }
  }

  drawFooter();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void SokobanGameActivity::drawHUD() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_SOKOBAN_TITLE));

  char status[96];
  snprintf(status, sizeof(status), "%s %d/%d · %s:%d %s:%d", tr(STR_SOKOBAN_LEVEL), currentLevel + 1, totalLevels,
           tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES), board.pushes);
  GUI.drawSubHeader(
      renderer,
      Rect{safeArea.x, safeArea.y + metrics.topPadding + metrics.headerHeight, safeArea.width, metrics.tabBarHeight},
      status);
}

void SokobanGameActivity::drawBoard() {
  if (board.rows == 0 || board.cols == 0) {
    LOG_ERR("SOK", "Empty board, skip drawing");
    return;
  }

  const Rect content = getContentArea(renderer, true, mappedInput.hasTouch());
  const BoardLayout layout =
      fitBoard(content.x, content.y, content.x + content.width, content.y + content.height, board.rows, board.cols);
  if (!layout.valid()) {
    LOG_ERR("SOK", "Board %dx%d does not fit content (%d,%d,%d,%d)", board.cols, board.rows, content.x, content.y,
            content.width, content.height);
    return;
  }

  for (int r = 0; r < board.rows; ++r) {
    for (int c = 0; c < board.cols; ++c) {
      const int cellSize = layout.cellSize;
      int x = layout.x + c * cellSize;
      int y = layout.y + r * cellSize;
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

void SokobanGameActivity::drawFooter() {
  if (state == State::Won) return;

  const char* previous = state == State::LevelSelect ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT);
  const char* next = state == State::LevelSelect ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SokobanGameActivity::drawWinScreen() {
  const int sh = renderer.getScreenHeight();
  char buf[64];
  snprintf(buf, sizeof(buf), "%s %d %s %d", tr(STR_SOKOBAN_MOVES), moves, tr(STR_SOKOBAN_PUSHES), board.pushes);

  constexpr int gap = 12;
  const int titleH = renderer.getTextHeight(NOTOSERIF_14_FONT_ID);
  const int bodyH = renderer.getTextHeight(UI_12_FONT_ID);
  const int blockH = titleH + gap + bodyH + gap + bodyH;
  const int titleY = gameCenteredBlockY(0, sh, blockH);
  renderer.drawCenteredText(NOTOSERIF_14_FONT_ID, titleY, tr(STR_SOKOBAN_WIN));
  renderer.drawCenteredText(UI_12_FONT_ID, titleY + titleH + gap, buf);
  renderer.drawCenteredText(UI_12_FONT_ID, titleY + titleH + gap + bodyH + gap, tr(STR_SOKOBAN_WIN_HINT));
}

void SokobanGameActivity::drawLevelSelect() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const LevelSelectLayout layout = getLevelSelectLayout(renderer, mappedInput.hasTouch());

  char title[64];
  snprintf(title, sizeof(title), "%s (%d)", tr(STR_SOKOBAN_TITLE), totalLevels);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 title);

  const int textHeight = renderer.getTextHeight(UI_12_FONT_ID);
  for (int i = 0; i < layout.visibleCount && (scrollOffset + i) < totalLevels; ++i) {
    int levelIdx = scrollOffset + i;
    int y = layout.content.y + i * layout.rowStep;

    if (levelIdx == selectedLevel) {
      renderer.fillRect(layout.content.x, y + 2, 4, std::max(1, layout.rowStep - 4), true);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d", tr(STR_SOKOBAN_LEVEL), levelIdx + 1);
    renderer.drawText(UI_12_FONT_ID, layout.content.x + 12, y + gameCenterY(layout.rowStep, textHeight), buf);
  }

  GUI.drawSideScrollBar(renderer, layout.content, totalLevels, scrollOffset, layout.visibleCount);
}
