#include "UITheme.h"

#include <EpdFont.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>

#include <algorithm>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "components/CoverGridHomeUi.h"
#include "components/SelectionCursorPolicy.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/inx/InxTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/roundedraff/RoundedRaffTheme.h"
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_regular.h>
#endif

// The registered families keep these stable addresses across theme changes.
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
EpdFont ui10RegularFont(&notosans_14_regular);
EpdFont ui10BoldFont(&notosans_14_bold);
EpdFont ui12RegularFont(&notosans_16_regular);
EpdFont ui12BoldFont(&notosans_16_bold);
#else
EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
#endif

extern EpdFont offlineReaderFont;
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
EpdFont ui18RegularFont(&notosans_16_regular);
EpdFont ui18BoldFont(&notosans_16_bold);
EpdFontFamily control18FontFamily(&ui12RegularFont, &ui12BoldFont);
#else
EpdFont ui18RegularFont(&notosans_18_regular);
EpdFont ui18BoldFont(&notosans_18_bold);

// Fixed control faces share the existing bitmap data; theme reload never mutates them.
static EpdFont control18RegularFont(&notosans_18_regular);
static EpdFont control18BoldFont(&notosans_18_bold);
EpdFontFamily control18FontFamily(&control18RegularFont, &control18BoldFont);

#endif

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  const bool inx = SETTINGS.uiTheme == CrossPointSettings::INX;
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  (void)inx;
  ui18RegularFont.data = &notosans_16_regular;
  ui18BoldFont.data = &notosans_16_bold;
#else
  ui18RegularFont.data = inx ? offlineReaderFont.data : &notosans_18_regular;
  ui18BoldFont.data = inx ? offlineReaderFont.data : &notosans_18_bold;
#endif
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

bool UITheme::supportsCoverGrid() { return HalMemory::getPsramHeap().totalBytes > 0; }

bool UITheme::hasCoverGridHome() { return SETTINGS.uiTheme == CrossPointSettings::COVER_GRID && supportsCoverGrid(); }

void UITheme::drawCoverGridHome(CoverGridHomeUi& home) { home.renderUi(); }

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  std::unique_ptr<BaseTheme> nextTheme;
  const ThemeMetrics* nextMetrics = &BaseMetrics::values;
  if (type == CrossPointSettings::COVER_GRID && !supportsCoverGrid()) type = CrossPointSettings::LYRA;

  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      nextTheme = makeUniqueNoThrow<BaseTheme>();
      break;
    case CrossPointSettings::UI_THEME::COVER_GRID:
    case CrossPointSettings::UI_THEME::LYRA:
      LOG_DBG("UI", "Using Lyra theme");
      nextTheme = makeUniqueNoThrow<LyraTheme>();
      nextMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::ROUNDEDRAFF:
      LOG_DBG("UI", "Using RoundedRaff theme");
      nextTheme = makeUniqueNoThrow<RoundedRaffTheme>();
      nextMetrics = &RoundedRaffMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_3_COVERS:
      LOG_DBG("UI", "Using Lyra 3 Covers theme");
      nextTheme = makeUniqueNoThrow<Lyra3CoversTheme>();
      nextMetrics = &Lyra3CoversMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_CAROUSEL:
      LOG_DBG("UI", "Using Lyra Carousel theme");
      nextTheme = makeUniqueNoThrow<LyraCarouselTheme>();
      nextMetrics = &LyraCarouselMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::INX:
      LOG_DBG("UI", "Using INX theme");
      nextTheme = makeUniqueNoThrow<InxTheme>();
      nextMetrics = &InxMetrics::values;
      break;
    default:
      LOG_ERR("UI", "Unknown theme %d, falling back to Classic", static_cast<int>(type));
      nextTheme = makeUniqueNoThrow<BaseTheme>();
      type = CrossPointSettings::UI_THEME::CLASSIC;
      break;
  }

  if (!nextTheme) {
    LOG_ERR("UI", "OOM creating theme %d; using static Classic fallback", static_cast<int>(type));
    ownedTheme.reset();
    currentTheme = &fallbackTheme;
    currentMetrics = &BaseMetrics::values;
    currentType = CrossPointSettings::UI_THEME::CLASSIC;
  } else {
    ownedTheme = std::move(nextTheme);
    currentTheme = ownedTheme.get();
    currentMetrics = nextMetrics;
    currentType = type;
  }
  metricsValid = false;
}

const ThemeMetrics& UITheme::getMetrics() const {
  // Touch availability can flip after static construction, and the setting can
  // change while this screen is open, so cache against the effective policy.
  const bool showButtonHints = currentTheme->buttonHintsVisible();
  if (!metricsValid || showButtonHints != metricsForButtonHints) {
    adjustedMetrics = *currentMetrics;
    UiHighDpiProfile::apply(adjustedMetrics);
    if (!showButtonHints) {
      adjustedMetrics.buttonHintsHeight = 0;
    }
    metricsForButtonHints = showButtonHints;
    metricsValid = true;
  }
  return adjustedMetrics;
}

bool UITheme::showSelectionCursor() const {
#ifdef CROSSPOINT_EMULATED
  return true;
#else
  return SelectionCursorPolicy::visible(currentType == CrossPointSettings::UI_THEME::INX, gpio.hasTouch(),
                                        gpio.lastInputModality());
#endif
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight) {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  auto orientation = renderer.getOrientation();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints && orientation != GfxRenderer::Orientation::LandscapeClockwise &&
      orientation != GfxRenderer::Orientation::LandscapeCounterClockwise) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight =
      UITheme::getInstance().getScreenSafeArea(renderer).height - reservedHeight - extraReservedHeight;
  return UITheme::getInstance().getTheme().getListPageItems(availableHeight, hasSubtitle);
}

// Screen area excluding the bezel and button hints.
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
#if FREEINK_DEVICE_READPICO
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  safeArea = Rect{left, top, screenWidth - left - right, screenHeight - top - bottom};
#endif
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();

  // Layout reservation is hardware-agnostic: pass clockAvailable=true so the
  // reserved height does not depend on whether an RTC is present.
  return (sb.textLaneVisible() ? (metrics.statusBarVerticalMargin) : 0) +
         (sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0);
}

int UITheme::getProgressBarHeight() {
  const ThemeMetrics metrics = UITheme::getInstance().getMetrics();
  const auto sb = SETTINGS.statusBarSpec();
  return sb.showsProgressBar() ? (sb.progressBarHeightPx + metrics.progressBarMarginTop) : 0;
}

int UITheme::getStatusBarTextTopPadding(const GfxRenderer& renderer) {
  if (!UiHighDpiProfile::enabled) return 0;
#if FREEINK_DEVICE_READPICO
  constexpr int textFontId = READER_STATUS_FONT_ID;
#else
  constexpr int textFontId = SMALL_FONT_ID;
#endif
  const int lineHeight =
      std::max(renderer.getLineHeight(textFontId), renderer.getLineHeight(BaseTheme::STATUS_NUMERIC_FONT_ID));
  return std::max(0, UITheme::getInstance().getMetrics().statusBarVerticalMargin - lineHeight -
                         UiHighDpiProfile::readerStatusBottomPadding);
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

void UITheme::drawCenteredWrappedText(const GfxRenderer& renderer, Rect bounds, int fontId, const char* text,
                                      int maxLines, bool black, EpdFontFamily::Style style,
                                      TextVerticalAlignment verticalAlignment) {
  if (!text || *text == '\0' || bounds.width <= 0 || bounds.height <= 0 || maxLines <= 0) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  if (lineHeight <= 0) return;

  const int lineLimit = std::min(maxLines, bounds.height / lineHeight);
  if (lineLimit <= 0) return;

  const auto alignedTop = [&](const int textHeight) {
    switch (verticalAlignment) {
      case TextVerticalAlignment::CENTER:
        return bounds.y + (bounds.height - textHeight) / 2;
      case TextVerticalAlignment::BOTTOM:
        return bounds.y + bounds.height - textHeight;
      case TextVerticalAlignment::TOP:
      default:
        return bounds.y;
    }
  };

  if (renderer.getTextWidth(fontId, text, style) <= bounds.width) {
    drawCenteredText(renderer, bounds, fontId, alignedTop(lineHeight), text, black, style);
    return;
  }

  const auto lines = renderer.wrappedText(fontId, text, bounds.width, lineLimit, style);
  int y = alignedTop(static_cast<int>(lines.size()) * lineHeight);
  for (const auto& line : lines) {
    drawCenteredText(renderer, bounds, fontId, y, line.c_str(), black, style);
    y += lineHeight;
  }
}
