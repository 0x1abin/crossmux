#include "ConwayGameOfLifeActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>

#include <cstdlib>
#include <cstring>

#include "components/UITheme.h"

namespace {
constexpr uint32_t kSeedDensityNumerator = 1;
constexpr uint32_t kSeedDensityDenominator = 4;  // ~25% live density on seed
}  // namespace

void ConwayGameOfLifeActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const int viewportTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int viewportBottom = sh - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int viewportLeft = metrics.contentSidePadding;
  const int viewportRight = sw - metrics.contentSidePadding;
  const int viewportW = viewportRight - viewportLeft;
  const int viewportH = viewportBottom - viewportTop;

  cols_ = viewportW / kCellPx;
  rows_ = viewportH / kCellPx;
  // Center the grid inside the viewport so leftover pixels don't pile on one side.
  gridOriginX_ = viewportLeft + (viewportW - cols_ * kCellPx) / 2;
  gridOriginY_ = viewportTop + (viewportH - rows_ * kCellPx) / 2;
  bytesPerRow_ = (cols_ + 7) / 8;

  if (!allocBuffers()) {
    LOG_ERR("CONWAY", "buffer alloc failed cols=%d rows=%d", cols_, rows_);
    activityManager.goToApps();
    return;
  }

  LOG_DBG("CONWAY", "grid %dx%d bytesPerRow=%d heap=%u", cols_, rows_, bytesPerRow_,
          static_cast<unsigned>(ESP.getFreeHeap()));

  randomize();
  running_ = false;
  generation_ = 0;
  lastStepMs_ = millis();
  requestUpdate();
}

void ConwayGameOfLifeActivity::onExit() {
  freeBuffers();
  LOG_DBG("CONWAY", "onExit heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  Activity::onExit();
}

bool ConwayGameOfLifeActivity::allocBuffers() {
  if (cols_ <= 0 || rows_ <= 0) return false;
  const size_t totalBytes = static_cast<size_t>(bytesPerRow_) * static_cast<size_t>(rows_);
  current_ = static_cast<uint8_t*>(malloc(totalBytes));
  if (!current_) {
    LOG_ERR("CONWAY", "malloc current_ failed: %u bytes", static_cast<unsigned>(totalBytes));
    return false;
  }
  next_ = static_cast<uint8_t*>(malloc(totalBytes));
  if (!next_) {
    LOG_ERR("CONWAY", "malloc next_ failed: %u bytes", static_cast<unsigned>(totalBytes));
    free(current_);
    current_ = nullptr;
    return false;
  }
  memset(current_, 0, totalBytes);
  memset(next_, 0, totalBytes);
  return true;
}

void ConwayGameOfLifeActivity::freeBuffers() {
  if (current_) {
    free(current_);
    current_ = nullptr;
  }
  if (next_) {
    free(next_);
    next_ = nullptr;
  }
}

bool ConwayGameOfLifeActivity::cellAt(int r, int c) const {
  const int idx = r * bytesPerRow_ + (c >> 3);
  return (current_[idx] >> (7 - (c & 7))) & 1;
}

void ConwayGameOfLifeActivity::setCell(int r, int c, bool alive, uint8_t* buf) {
  const int idx = r * bytesPerRow_ + (c >> 3);
  const uint8_t mask = static_cast<uint8_t>(1u << (7 - (c & 7)));
  if (alive) {
    buf[idx] |= mask;
  } else {
    buf[idx] &= static_cast<uint8_t>(~mask);
  }
}

int ConwayGameOfLifeActivity::countNeighbors(int r, int c) const {
  int n = 0;
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      if (dr == 0 && dc == 0) continue;
      int nr = r + dr;
      int nc = c + dc;
      if (nr < 0) nr += rows_;
      else if (nr >= rows_) nr -= rows_;
      if (nc < 0) nc += cols_;
      else if (nc >= cols_) nc -= cols_;
      if (cellAt(nr, nc)) ++n;
    }
  }
  return n;
}

void ConwayGameOfLifeActivity::randomize() {
  if (!current_) return;
  const size_t totalBytes = static_cast<size_t>(bytesPerRow_) * static_cast<size_t>(rows_);
  memset(current_, 0, totalBytes);
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      if ((esp_random() % kSeedDensityDenominator) < kSeedDensityNumerator) {
        setCell(r, c, true, current_);
      }
    }
  }
  generation_ = 0;
}

void ConwayGameOfLifeActivity::step() {
  if (!current_ || !next_) return;
  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      const int n = countNeighbors(r, c);
      const bool alive = cellAt(r, c);
      const bool nextAlive = (alive && (n == 2 || n == 3)) || (!alive && n == 3);
      setCell(r, c, nextAlive, next_);
    }
  }
  uint8_t* tmp = current_;
  current_ = next_;
  next_ = tmp;
  ++generation_;
}

void ConwayGameOfLifeActivity::cycleSpeed() {
  if (stepIntervalMs_ == kSpeedSlowMs) {
    stepIntervalMs_ = kSpeedMediumMs;
  } else if (stepIntervalMs_ == kSpeedMediumMs) {
    stepIntervalMs_ = kSpeedFastMs;
  } else {
    stepIntervalMs_ = kSpeedSlowMs;
  }
}

void ConwayGameOfLifeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    running_ = !running_;
    lastStepMs_ = millis();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    randomize();
    running_ = false;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    cycleSpeed();
    requestUpdate();
    return;
  }

  if (running_ && current_) {
    const uint32_t now = millis();
    if (now - lastStepMs_ >= static_cast<uint32_t>(stepIntervalMs_)) {
      step();
      lastStepMs_ = now;
      requestUpdate();
    }
  }
}

void ConwayGameOfLifeActivity::render(RenderLock&&) {
  if (!current_) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();

  renderer.clearScreen();

  char headerBuf[64];
  const char* stateLabel = running_ ? tr(STR_PLAYING) : tr(STR_PAUSED);
  snprintf(headerBuf, sizeof(headerBuf), "%s  ·  Gen %u  ·  %s", tr(STR_CONWAY_TITLE),
           static_cast<unsigned>(generation_), stateLabel);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, headerBuf);

  // Draw a thin frame around the grid area so the simulation field is visible
  // even when no cells are alive.
  renderer.drawRect(gridOriginX_ - 1, gridOriginY_ - 1, cols_ * kCellPx + 2, rows_ * kCellPx + 2, true);

  for (int r = 0; r < rows_; ++r) {
    for (int c = 0; c < cols_; ++c) {
      if (cellAt(r, c)) {
        renderer.fillRect(gridOriginX_ + c * kCellPx, gridOriginY_ + r * kCellPx, kCellPx - 1, kCellPx - 1, true);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), running_ ? tr(STR_PAUSE) : tr(STR_PLAY), tr(STR_SPEED),
                                            tr(STR_RANDOM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const HalDisplay::RefreshMode mode =
      (generation_ > 0 && (generation_ % kFullRefreshEvery) == 0) ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH;
  renderer.displayBuffer(mode);
}
