#include "FontPreloadView.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fontpreload {

void draw(const GfxRenderer& renderer, const char* familyName, const uint8_t pointSize, const size_t completed,
          const size_t total, const State state) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, false, false);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_FONT_PRELOADING));
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  GfxRenderer::ClipScope contentClip(renderer, content.x, content.y, content.width, content.height);

  if (state == State::Ready) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_FONT_CACHE_READY), true, EpdFontFamily::BOLD);
    return;
  }

  char fontLine[48];
  snprintf(fontLine, sizeof(fontLine), "%s, %u pt", familyName, static_cast<unsigned>(pointSize));

  const size_t boundedCompleted = std::min(completed, total);
  const size_t sourceSize = total > 1 ? total / 2 : 0;
  const size_t phaseCompleted = boundedCompleted > sourceSize ? std::min(boundedCompleted - sourceSize, sourceSize)
                                                              : std::min(boundedCompleted, sourceSize);
  const int blockHeight = lineHeight + sectionGap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight) +
                          relatedGap + lineHeight + sectionGap + lineHeight;
  int y = SubpageLayout::centeredTop(content, blockHeight);
  UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, fontLine);
  y += lineHeight + sectionGap;
  const int detailY = GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight},
                                          boundedCompleted, total) +
                      relatedGap;

  char byteLine[40];
  snprintf(byteLine, sizeof(byteLine), "%u / %u KiB", static_cast<unsigned>((phaseCompleted + 1023) / 1024),
           static_cast<unsigned>((sourceSize + 1023) / 1024));
  UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, detailY, byteLine);
  UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, detailY + lineHeight + sectionGap,
                            tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
}

}  // namespace fontpreload
