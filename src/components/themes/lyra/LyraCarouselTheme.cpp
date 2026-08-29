#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCenterCoverWidth = 380;
constexpr int kCenterCoverHeight = 540;
constexpr int kCoverTopPadding = 32;
constexpr int kCornerRadius = 6;
constexpr int kActiveOutlineWidth = 3;
constexpr int kDotSize = 8;
constexpr int kDotGap = 6;
constexpr int kMenuIconSize = 32;
constexpr int kMenuIconPadding = 14;
constexpr int kMenuHighlightPadding = 7;

void drawAppsMenuIcon(const GfxRenderer& renderer, int x, int y, bool selected) {
  constexpr int kInset = 2;
  constexpr int kCellSize = 13;
  constexpr int kCellGap = 2;
  static_assert(kInset * 2 + kCellSize * 2 + kCellGap == kMenuIconSize);

  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      renderer.drawRoundedRect(x + kInset + column * (kCellSize + kCellGap), y + kInset + row * (kCellSize + kCellGap),
                               kCellSize, kCellSize, 2, 2, !selected);
    }
  }
}

void drawCover(const GfxRenderer& renderer, const RecentBook& book, int x, int y, int width, int height) {
  if (!book.coverBmpPath.empty()) {
    const std::string coverPath =
        UITheme::getCoverThumbPath(book.coverBmpPath, LyraCarouselMetrics::values.homeCoverHeight);
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        const float scale = std::min(
            {1.0f, static_cast<float>(width) / bitmap.getWidth(), static_cast<float>(height) / bitmap.getHeight()});
        const int drawWidth = std::max(1, static_cast<int>(bitmap.getWidth() * scale));
        const int drawHeight = std::max(1, static_cast<int>(bitmap.getHeight() * scale));
        const int drawX = x + (width - drawWidth) / 2;
        const int drawY = y + (height - drawHeight) / 2;
        renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
        renderer.maskRoundedRectOutsideCorners(drawX, drawY, drawWidth, drawHeight, kCornerRadius, Color::White);
        return;
      }
    }
  }

  renderer.drawRoundedRect(x, y, width, height, 1, kCornerRadius, true);
  renderer.fillRoundedRect(x, y + height / 3, width, 2 * height / 3, kCornerRadius, false, false, true, true,
                           Color::Black);
  renderer.drawIcon(CoverIcon, x + (width - kMenuIconSize) / 2, y + height / 6 - kMenuIconSize / 2, kMenuIconSize);
}
}  // namespace

void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                            std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  (void)storeCoverBuffer;
  coverBufferStored = false;
  if (recentBooks.empty()) {
    lastCenterIndex_ = -1;
    drawEmptyRecents(renderer, rect);
    coverRendered = true;
    return;
  }

  const GfxRenderer::ClipScope clip(renderer, rect.x, rect.y, rect.width, rect.height);
  const int bookCount = static_cast<int>(recentBooks.size());
  const bool coversFocused = selectorIndex >= 0 && selectorIndex < bookCount;
  int centerIndex = coversFocused ? selectorIndex : lastCenterIndex_;
  centerIndex = std::clamp(centerIndex, 0, bookCount - 1);

  if (centerIndex != lastCenterIndex_) {
    coverRendered = false;
    coverBufferStored = false;
    lastCenterIndex_ = centerIndex;
  }

  const int centerX = (renderer.getScreenWidth() - kCenterCoverWidth) / 2;
  const int centerY = rect.y + kCoverTopPadding;

  if (!coverRendered) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

    drawCover(renderer, recentBooks[centerIndex], centerX, centerY, kCenterCoverWidth, kCenterCoverHeight);

    const int dotsY = centerY + kCenterCoverHeight + 8;
    const int dotsWidth = bookCount * kDotSize + (bookCount - 1) * kDotGap;
    int dotX = centerX + (kCenterCoverWidth - dotsWidth) / 2;
    for (int i = 0; i < bookCount; ++i) {
      if (i == centerIndex) {
        renderer.fillRect(dotX, dotsY, kDotSize, kDotSize, true);
      } else {
        renderer.drawRect(dotX, dotsY, kDotSize, kDotSize, true);
      }
      dotX += kDotSize + kDotGap;
    }

    int textY = dotsY + kDotSize + 6;
    if (!recentBooks[centerIndex].author.empty()) {
      const std::string author =
          renderer.truncatedText(UI_10_FONT_ID, recentBooks[centerIndex].author.c_str(), kCenterCoverWidth);
      const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, author.c_str());
      renderer.drawText(UI_10_FONT_ID, centerX + (kCenterCoverWidth - authorWidth) / 2, textY, author.c_str(), true);
      textY += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    }

    const std::string title = renderer.truncatedText(UI_12_FONT_ID, recentBooks[centerIndex].title.c_str(),
                                                     kCenterCoverWidth, EpdFontFamily::BOLD);
    const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, centerX + (kCenterCoverWidth - titleWidth) / 2, textY, title.c_str(), true,
                      EpdFontFamily::BOLD);

    coverRendered = true;
  }

  if (coversFocused) {
    renderer.drawRoundedRect(centerX, centerY, kCenterCoverWidth, kCenterCoverHeight, kActiveOutlineWidth,
                             kCornerRadius, true);
  }
}

void LyraCarouselTheme::drawHomeMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                     const std::function<std::string(int index)>& buttonLabel,
                                     const std::function<UIIcon(int index)>& rowIcon) const {
  (void)rect;
  (void)buttonLabel;
  if (buttonCount <= 0) return;

  const int tileHeight = kMenuIconPadding * 2 + kMenuIconSize;
  const int tileWidth = renderer.getScreenWidth() / buttonCount;
  const int rowY = renderer.getScreenHeight() - tileHeight;

  for (int i = 0; i < buttonCount; ++i) {
    const int tileX = i * tileWidth;
    const int iconX = tileX + (tileWidth - kMenuIconSize) / 2;
    const int iconY = rowY + kMenuIconPadding;
    const bool selected = i == selectedIndex;
    if (selected) {
      const int highlightSize = kMenuIconSize + kMenuHighlightPadding * 2;
      renderer.fillRoundedRect(iconX - kMenuHighlightPadding, rowY + (tileHeight - highlightSize) / 2, highlightSize,
                               highlightSize, kCornerRadius, Color::Black);
    }

    if (rowIcon == nullptr) continue;
    const UIIcon iconName = rowIcon(i);
    if (iconName == UIIcon::Apps) {
      drawAppsMenuIcon(renderer, iconX, iconY, selected);
      continue;
    }

    const uint8_t* icon = iconForName(iconName, kMenuIconSize);
    if (icon == nullptr) continue;
    if (selected) {
      renderer.drawIconInverted(icon, iconX, iconY, kMenuIconSize);
    } else {
      renderer.drawIcon(icon, iconX, iconY, kMenuIconSize);
    }
  }
}

void LyraCarouselTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                                 const std::function<std::string(int index)>& rowTitle,
                                 const std::function<std::string(int index)>& rowSubtitle,
                                 const std::function<UIIcon(int index)>& rowIcon,
                                 const std::function<std::string(int index)>& rowValue, bool highlightValue,
                                 const std::function<bool(int index)>& rowDimmed, const bool showSelection,
                                 const std::function<bool(int index)>&) const {
  drawListWithMetrics(renderer, rect, itemCount, selectedIndex, rowTitle, rowSubtitle, rowIcon, rowValue,
                      highlightValue, rowDimmed, LyraCarouselMetrics::values, true, showSelection);
}
