#include "WoodfishActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "WoodfishAssets.h"
#include "WoodfishStore.h"

namespace {

constexpr uint32_t kFeedbackMs = 210;
constexpr uint32_t kIdleSaveMs = 60000;
constexpr int kPortraitCounterHeight = 190;
constexpr sloppy::Style kCounterStyle{sloppy::AlphabetId::Geometric, 0.0f, 7, 0.0f, 0.0f, 18, false};

// Source-space bounds of the wooden body and stand. The mallet, ripples, and
// surrounding whitespace are visual feedback, not touch targets.
constexpr int kHitSourceX = 72;
constexpr int kHitSourceY = 78;
constexpr int kHitSourceWidth = 224;
constexpr int kHitSourceHeight = 166;

void drawCenteredText(const GfxRenderer& renderer, const int fontId, const Rect& bounds, const int y, const char* text,
                      const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, bounds.x + (bounds.width - width) / 2, y, text, true, style);
}

void drawPackedBitmap(const GfxRenderer& renderer, const uint8_t* bitmap, const int bitmapWidth, const int sourceX,
                      const int sourceY, const int sourceWidth, const int sourceHeight, const Rect& destination) {
  if (destination.width <= 0 || destination.height <= 0) return;

  const int rowBytes = (bitmapWidth + 7) / 8;
  for (int y = 0; y < destination.height; ++y) {
    const int bitmapY = sourceY + y * sourceHeight / destination.height;
    for (int x = 0; x < destination.width; ++x) {
      const int bitmapX = sourceX + x * sourceWidth / destination.width;
      if ((bitmap[bitmapY * rowBytes + bitmapX / 8] & (0x80 >> (bitmapX % 8))) != 0) {
        renderer.drawPixel(destination.x + x, destination.y + y);
      }
    }
  }
}

void drawFeedbackBitmap(const GfxRenderer& renderer, const Rect& bounds, int desiredHeight) {
  if (bounds.width <= 0 || bounds.height <= 0) return;

  desiredHeight = std::min(desiredHeight, bounds.height);
  int cellWidth =
      std::max(1, desiredHeight * WoodfishAssets::kFeedbackGlyphWidth / WoodfishAssets::kFeedbackGlyphHeight);
  int gap = std::max(2, desiredHeight / 16);
  int totalWidth = cellWidth * 2 + gap;
  if (totalWidth > bounds.width) {
    desiredHeight = std::max(1, desiredHeight * bounds.width / totalWidth);
    cellWidth = std::max(1, desiredHeight * WoodfishAssets::kFeedbackGlyphWidth / WoodfishAssets::kFeedbackGlyphHeight);
    gap = std::max(1, desiredHeight / 16);
    totalWidth = cellWidth * 2 + gap;
  }

  const int x = bounds.x + (bounds.width - totalWidth) / 2;
  const int y = bounds.y + (bounds.height - desiredHeight) / 2;
  drawPackedBitmap(renderer, WoodfishAssets::kFeedbackAtlas,
                   WoodfishAssets::kFeedbackGlyphWidth * WoodfishAssets::kFeedbackGlyphCount, 0, 0,
                   WoodfishAssets::kFeedbackGlyphWidth, WoodfishAssets::kFeedbackGlyphHeight,
                   Rect{x, y, cellWidth, desiredHeight});
  drawPackedBitmap(renderer, WoodfishAssets::kFeedbackAtlas,
                   WoodfishAssets::kFeedbackGlyphWidth * WoodfishAssets::kFeedbackGlyphCount,
                   WoodfishAssets::kFeedbackGlyphWidth, 0, WoodfishAssets::kFeedbackGlyphWidth,
                   WoodfishAssets::kFeedbackGlyphHeight, Rect{x + cellWidth + gap, y, cellWidth, desiredHeight});
}

}  // namespace

void WoodfishActivity::onEnter() {
  Activity::onEnter();
  sloppy::prepareSeeds(/*seed=*/1u, kCounterStyle, digitSeeds_);
  if (!WoodfishStore::load(total_)) total_ = 0;
  dirty_ = false;
  feedbackActive_ = false;
  feedbackShowsIncrement_ = false;
  requestUpdate();
}

void WoodfishActivity::onExit() {
  if (dirty_) persist();
  Activity::onExit();
}

void WoodfishActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }

  const uint32_t now = millis();
  int touchX = 0;
  int touchY = 0;
  const bool woodfishTapped =
      mappedInput.wasScreenTapped(touchX, touchY) && woodfishHitBounds_.contains(touchX, touchY);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down) || woodfishTapped) {
    knock(now);
  }

  if (feedbackActive_ && static_cast<uint32_t>(now - feedbackStartedMs_) >= kFeedbackMs) {
    feedbackActive_ = false;
    feedbackShowsIncrement_ = false;
    requestUpdate();
  }

  if (dirty_ && static_cast<uint32_t>(now - saveIdleSinceMs_) >= kIdleSaveMs) {
    if (!persist()) saveIdleSinceMs_ = now;
  }
}

void WoodfishActivity::knock(const uint32_t now) {
  feedbackShowsIncrement_ = total_ != UINT32_MAX;
  if (feedbackShowsIncrement_) {
    ++total_;
    dirty_ = true;
    saveIdleSinceMs_ = now;
  }
  feedbackActive_ = true;
  feedbackStartedMs_ = now;
  requestUpdate();
}

bool WoodfishActivity::persist() {
  if (!dirty_) return true;
  if (!WoodfishStore::save(total_)) {
    LOG_ERR("WDF", "Failed to save total merit");
    return false;
  }
  dirty_ = false;
  return true;
}

void WoodfishActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentInset = metrics.contentSidePadding;
  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const Rect content{safe.x + contentInset, contentTop, std::max(1, safe.width - contentInset * 2),
                     std::max(1, safe.y + safe.height - contentTop - metrics.verticalSpacing)};

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_WOODFISH_TITLE));

  if (content.height >= content.width) {
    const int counterHeight = std::min(kPortraitCounterHeight, content.height / 2);
    const int artHeight = content.height - counterHeight;
    drawWoodfish(Rect{content.x, content.y, content.width, artHeight});
    renderer.drawLine(content.x + content.width / 6, content.y + artHeight, content.x + content.width * 5 / 6,
                      content.y + artHeight, true);
    drawTotal(Rect{content.x, content.y + artHeight + 1, content.width, counterHeight - 1});
  } else {
    const int artWidth = content.width * 56 / 100;
    drawWoodfish(Rect{content.x, content.y, artWidth, content.height});
    renderer.drawLine(content.x + artWidth, content.y + content.height / 8, content.x + artWidth,
                      content.y + content.height * 7 / 8, true);
    drawTotal(Rect{content.x + artWidth + 1, content.y, content.width - artWidth - 1, content.height});
  }

  const char* addOne = tr(STR_WOODFISH_ADD_ONE);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), addOne, addOne, addOne);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void WoodfishActivity::drawWoodfish(const Rect& bounds) {
  int artWidth = std::min(bounds.width, WoodfishAssets::kArtWidth);
  int artHeight = artWidth * WoodfishAssets::kArtHeight / WoodfishAssets::kArtWidth;
  if (artHeight > bounds.height) {
    artHeight = bounds.height;
    artWidth = artHeight * WoodfishAssets::kArtWidth / WoodfishAssets::kArtHeight;
  }
  const Rect art{bounds.x + (bounds.width - artWidth) / 2, bounds.y + (bounds.height - artHeight) / 2, artWidth,
                 artHeight};
  woodfishHitBounds_ = {art.x + kHitSourceX * art.width / WoodfishAssets::kArtWidth,
                        art.y + kHitSourceY * art.height / WoodfishAssets::kArtHeight,
                        kHitSourceWidth * art.width / WoodfishAssets::kArtWidth,
                        kHitSourceHeight * art.height / WoodfishAssets::kArtHeight};
  drawPackedBitmap(renderer, feedbackActive_ ? WoodfishAssets::kStrikeArt : WoodfishAssets::kIdleArt,
                   WoodfishAssets::kArtWidth, 0, 0, WoodfishAssets::kArtWidth, WoodfishAssets::kArtHeight, art);

  if (feedbackShowsIncrement_) {
    constexpr int feedbackHeight = 34;
    const int bodyTop = art.y + art.height * 81 / WoodfishAssets::kArtHeight;
    const int feedbackY = std::max(bounds.y, bodyTop - feedbackHeight - 8);
    drawFeedbackBitmap(renderer, Rect{bounds.x, feedbackY, bounds.width, feedbackHeight}, feedbackHeight);
  }
}

void WoodfishActivity::drawTotal(const Rect& bounds) const {
  const int labelHeight = renderer.getTextHeight(UI_12_FONT_ID);
  const int gap = std::max(5, bounds.height / 18);
  drawCenteredText(renderer, UI_12_FONT_ID, bounds, bounds.y + gap, tr(STR_WOODFISH_TOTAL_MERIT), EpdFontFamily::BOLD);

  char value[11];
  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(total_));
  const int digitCount = static_cast<int>(std::strlen(value));
  const int desiredHeight = digitCount >= 9 ? 58 : digitCount >= 7 ? 72 : 96;
  const int numberY = bounds.y + labelHeight + gap * 2;
  const Rect numberBounds{bounds.x, numberY, bounds.width, std::max(1, bounds.y + bounds.height - numberY - gap)};
  const int numberHeight = std::min(desiredHeight, numberBounds.height);
  sloppy::draw(renderer, kCounterStyle, digitSeeds_, value,
               sloppy::Bounds{numberBounds.x, numberBounds.y + (numberBounds.height - numberHeight) / 2,
                              numberBounds.width, numberHeight});
}
