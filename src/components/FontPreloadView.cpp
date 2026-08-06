#include "FontPreloadView.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace fontpreload {

void draw(const GfxRenderer& renderer, const char* familyName, const uint8_t pointSize, const size_t completed,
          const size_t total, const State state) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = pageHeight / 2 - lineHeight;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_PRELOADING));

  if (state == State::Ready) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_FONT_CACHE_READY), true, EpdFontFamily::BOLD);
    return;
  }

  char fontLine[48];
  snprintf(fontLine, sizeof(fontLine), "%s, %u pt", familyName, static_cast<unsigned>(pointSize));
  renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, fontLine);

  const size_t boundedCompleted = std::min(completed, total);
  const size_t sourceSize = total > 1 ? total / 2 : 0;
  const size_t phaseCompleted = boundedCompleted > sourceSize ? std::min(boundedCompleted - sourceSize, sourceSize)
                                                              : std::min(boundedCompleted, sourceSize);
  const int barY = centerY + metrics.verticalSpacing;
  const int detailY = GUI.drawProgressBar(renderer,
                                          Rect{metrics.contentSidePadding, barY,
                                               pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                                          boundedCompleted, total) +
                      4;

  char byteLine[40];
  snprintf(byteLine, sizeof(byteLine), "%u / %u KiB", static_cast<unsigned>((phaseCompleted + 1023) / 1024),
           static_cast<unsigned>((sourceSize + 1023) / 1024));
  renderer.drawCenteredText(UI_10_FONT_ID, detailY, byteLine);
  renderer.drawCenteredText(UI_10_FONT_ID, detailY + lineHeight + 4, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
}

}  // namespace fontpreload
