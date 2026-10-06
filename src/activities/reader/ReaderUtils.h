#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <components/bars/tap-zones.h>
#include <components/themes/BaseTheme.h>

#include <cctype>
#include <string_view>

#include "MappedInputManager.h"
#include "ReaderRefresh.h"
#include "activities/ActivityManager.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

inline bool gestureAllowsSwipe(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::SWIPE_ONLY;
}

inline bool gestureAllowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY ||
         gesture == CrossPointSettings::INVERTED_TAP;
}

inline bool isRtlBookLanguage(std::string_view tag) {
  if (tag.size() < 2 || (tag.size() > 2 && tag[2] != '-' && tag[2] != '_')) return false;
  const auto first = std::tolower(static_cast<unsigned char>(tag[0]));
  const auto second = std::tolower(static_cast<unsigned char>(tag[1]));
  return (first == 'h' && second == 'e') || (first == 'i' && second == 'w') || (first == 'a' && second == 'r') ||
         (first == 'f' && second == 'a');
}

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool swapFront = input.isNavDirectionSwapped();
  const auto prevButton = swapFront ? MappedInputManager::Button::Right : MappedInputManager::Button::Left;
  const auto nextButton = swapFront ? MappedInputManager::Button::Left : MappedInputManager::Button::Right;
  const auto pageButtonTriggered = [&](const MappedInputManager::Button button) {
    if (usePress) return input.wasPressed(button);
    return input.wasLongPressed(button, SKIP_HOLD_MS) || input.wasReleased(button);
  };
  const bool prev =
      tiltPrev || (pageButtonTriggered(MappedInputManager::Button::PageBack) || pageButtonTriggered(prevButton));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = input.homeButtonAction() == HomeButtonAction::NextPage || tiltNext ||
                    pageButtonTriggered(MappedInputManager::Button::PageForward) || powerTurn ||
                    pageButtonTriggered(nextButton);
  return {prev, next, tiltPrev || tiltNext};
}

struct TouchPageTurn {
  bool prev = false;
  bool next = false;
  bool bookmark = false;
  bool dictionary = false;
  bool longPress = false;  // the contact was held past BOOKMARK_HOLD_MS
  uint8_t action = 0;      // short-press zone action (TAP_ZONE_ACTION, PREV/NEXT/MENU handled here)
  uint8_t longAction = 0;  // long-press zone action (TAP_ZONE_LONG_ACTION)
  unsigned long heldMs = 0;
};

// Shared tap-zone grid geometry: the 3x3 main grid plus six small
// corner/edge zones. The reader hit-tests with the exact rectangles the zone
// editor paints — same safe margin, visible gaps and cell sizes — so a tap in
// the reader lands on the zone the editor showed, and a tap in a gap or at the
// display edge is ignored instead of snapping to a neighbouring cell at
// non-divisible sizes.
struct TapZoneGrid {
  static constexpr int kSafeMargin = 6;  // inset from the grid area edges
  static constexpr int kGap = 6;         // visible gap between cells
  static constexpr int kMiniCount = 6;   // corner + edge-middle small zones

  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int cellWidth = 0;
  int cellHeight = 0;
  int miniWidth = 0;
  int miniHeight = 0;
  int miniMidWidth = 0;  // top/bottom edge-middle zones: wide strips
  int miniMidHeight = 0;

  explicit TapZoneGrid(const int gridW, const int gridH) {
    x = kSafeMargin;
    y = kSafeMargin;
    width = gridW - kSafeMargin * 2;
    height = gridH - kSafeMargin * 2;
    // Two internal gaps per axis; the remainder (if the size is not exactly
    // divisible) widens the outer gaps, never the visible cells.
    cellWidth = (width - kGap * 2) / 3;
    cellHeight = (height - kGap * 2) / 3;
    // Corner zones: one third of a main cell wide, one quarter tall (area stays
    // below 1/9 of a main cell), floored to a minimum touch target.
    miniWidth = cellWidth / 3;
    miniHeight = cellHeight / 4;
    if (miniWidth < 40) miniWidth = 40;
    if (miniHeight < 44) miniHeight = 44;
    // Top/bottom edge-middle zones: a wide horizontal strip instead of a tall
    // sliver — easier to hit and less likely to be brushed accidentally —
    // keeping the same area budget as the corner zones (below 1/9 of a cell).
    miniMidWidth = (cellWidth * 2) / 3;
    miniMidHeight = miniHeight / 2;
    if (miniMidHeight < 40) miniMidHeight = 40;
  }

  Rect cell(const int row, const int col) const {
    return Rect{static_cast<int16_t>(x + col * (cellWidth + kGap)), static_cast<int16_t>(y + row * (cellHeight + kGap)),
                static_cast<int16_t>(cellWidth), static_cast<int16_t>(cellHeight)};
  }

  // One of the six small zones, index 0..5:
  //   0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right,
  //   4 top-middle, 5 bottom-middle.
  // All are placed inside the safe margin, so they never touch the display
  // edge and never overlap the gap between main cells.
  Rect miniCell(const int index) const {
    int cx = 0;
    int cy = 0;
    switch (index) {
      case 1:
        cx = x + width - miniWidth;
        cy = y;
        break;
      case 2:
        cx = x;
        cy = y + height - miniHeight;
        break;
      case 3:
        cx = x + width - miniWidth;
        cy = y + height - miniHeight;
        break;
      case 4:
        cx = x + (width - miniMidWidth) / 2;
        cy = y;
        break;
      case 5:
        cx = x + (width - miniMidWidth) / 2;
        cy = y + height - miniMidHeight;
        break;
      case 0:
      default:
        cx = x;
        cy = y;
        break;
    }
    const bool mid = index == 4 || index == 5;
    return Rect{static_cast<int16_t>(cx), static_cast<int16_t>(cy),
                static_cast<int16_t>(mid ? miniMidWidth : miniWidth),
                static_cast<int16_t>(mid ? miniMidHeight : miniHeight)};
  }

  // Zone index for a point, or -1 when it lands in a gap or outside the grid.
  // The small zones win over the main grid where they overlap (they are nested
  // inside the corner/edge main cells), giving them priority as hot spots.
  int zoneAt(const int px, const int py) const {
    for (int i = 0; i < kMiniCount; ++i) {
      const Rect r = miniCell(i);
      if (px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height) {
        return 9 + i;
      }
    }
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const Rect r = cell(row, col);
        if (px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height) {
          return row * 3 + col;
        }
      }
    }
    return -1;
  }
};

inline int tapZoneAt(const GfxRenderer& renderer, const int x, const int y) {
  const int16_t width = static_cast<int16_t>(renderer.getScreenWidth());
  const int16_t height = static_cast<int16_t>(renderer.getScreenHeight());
  if (width <= 0 || height <= 0) return -1;
  return TapZoneGrid(width, height).zoneAt(x, y);
}

// The main-grid zone a mini zone inherits from when its own action is NONE:
// each corner / edge-middle small zone maps to the main cell it sits in.
inline int miniFallbackZone(const int zone) {
  switch (zone - 9) {
    case 0:
      return 0;  // top-left     -> top-left main cell
    case 1:
      return 2;  // top-right    -> top-right main cell
    case 2:
      return 6;  // bottom-left  -> bottom-left main cell
    case 3:
      return 8;  // bottom-right -> bottom-right main cell
    case 4:
      return 1;  // top-middle   -> top-middle main cell
    case 5:
      return 7;  // bottom-middle-> bottom-middle main cell
    default:
      return -1;
  }
}

// Raw configured short-press action of a zone (0..14), without the mini-zone
// fallback: what the user picked in the zone editor. A mini zone set to NONE
// reads back as NONE here even though taps still fall back to the main cell.
inline uint8_t zoneRawShortAction(const int zone) {
  if (zone < 0) return CrossPointSettings::TAP_ZONE_NONE;
  return zone < 9 ? SETTINGS.tapZones[zone] : SETTINGS.miniZones[zone - 9];
}

// Short-press action of a zone (0..14). Zones 0..8 are the main grid, 9..14
// are the small corner/edge zones. A mini zone with no action of its own
// reuses the action of the main cell it sits in, so a small zone never
// dead-ends unless the main cell is unset too.
inline uint8_t zoneShortAction(const int zone) {
  if (zone < 0) return CrossPointSettings::TAP_ZONE_NONE;
  if (zone < 9) return SETTINGS.tapZones[zone];
  const uint8_t own = SETTINGS.miniZones[zone - 9];
  if (own != CrossPointSettings::TAP_ZONE_NONE) return own;
  const int fallback = miniFallbackZone(zone);
  return fallback >= 0 ? SETTINGS.tapZones[fallback] : CrossPointSettings::TAP_ZONE_NONE;
}

// Long-press action of a zone (0..14), or TAP_ZONE_LONG_NONE.
inline uint8_t zoneLongAction(const int zone) {
  // Mini zones are tap-only: long-press actions exist for the main 3x3 grid.
  if (zone < 0 || zone >= 9) return CrossPointSettings::TAP_ZONE_LONG_NONE;
  return SETTINGS.tapZonesLong[zone];
}

// Action of the reader tap zone at the given screen point. The screen is
// split into the same full-screen grid the zone editor paints: inset by the
// safe margin, separated by visible gaps. A tap in a gap or in the safe
// margin falls through to TAP_ZONE_NONE instead of snapping to a neighbouring
// cell. The reading surface has no bottom button-hint row, so the grid covers
// the full display exactly like the original outer-thirds zones did.
inline uint8_t tapZoneAction(const GfxRenderer& renderer, const int x, const int y) {
  return zoneShortAction(tapZoneAt(renderer, x, y));
}

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input,
                                         const bool rtlBook = false) {
  TouchPageTurn result;
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  // 翻页方向（左右对调，给左手握持的用户）与 rtlBook 做 XOR：只开一个就转一次，两个都开
  // 就抵消 —— 一本由右往左读的书在左撇子模式下仍然是"往回翻"的那个方向，不会转两次变成没转。
  // / The page-turn direction mirrors the reader for the other hand. XOR with rtlBook so
  // exactly one of them flips the result and both together cancel: a right-to-left book in
  // mirrored mode still turns the way that book turns.
  const bool mirrored = rtlBook != (SETTINGS.pageTurnDirection != 0);

  // A slow swipe never becomes a long-press chapter skip.
  const auto dir = input.wasSwipe();
  if (dir != MappedInputManager::SwipeDir::None) {
    result.next = dir == (mirrored ? MappedInputManager::SwipeDir::Right : MappedInputManager::SwipeDir::Left) &&
                  gestureAllowsSwipe(SETTINGS.pageTurnGesture);
    result.prev = dir == (mirrored ? MappedInputManager::SwipeDir::Left : MappedInputManager::SwipeDir::Right) &&
                  gestureAllowsSwipe(SETTINGS.previousPageGesture);
    return result;
  }

  const bool nextTaps = gestureAllowsTap(SETTINGS.pageTurnGesture);
  const bool prevTaps = gestureAllowsTap(SETTINGS.previousPageGesture);
  if (!nextTaps && !prevTaps) {
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  // Tap-zone lookup, evaluated on release (the SDK latches the contact
  // duration at release). A long press (held past BOOKMARK_HOLD_MS) runs the
  // zone's long-press action; a zone without one keeps its short action. A
  // zone marked for a direction only acts when that direction's gesture
  // accepts taps; MENU zones are consumed by isTouchMenuGesture and never
  // turn pages here.
  const int zone = tapZoneAt(renderer, x, y);
  if (zone < 0) return result;

  result.heldMs = gpio.lastTouchHeldMs();

  // Long-press actions only exist on the main 3x3 grid; a held tap on a
  // mini zone behaves exactly like its short press.
  if (zone < 9 && result.heldMs >= BOOKMARK_HOLD_MS) {
    const uint8_t longAction = zoneLongAction(zone);
    if (longAction != CrossPointSettings::TAP_ZONE_LONG_NONE) {
      result.longPress = true;
      result.longAction = longAction;
      result.bookmark = longAction == CrossPointSettings::TAP_ZONE_LONG_BOOKMARK;
      result.dictionary = longAction == CrossPointSettings::TAP_ZONE_LONG_DICTIONARY;
      return result;
    }
    // No long-press action configured: a BOOKMARK/DICTIONARY short zone keeps
    // its original long-press behaviour, everything else falls through to the
    // short action below (PREV/NEXT long-press still turns pages).
    const uint8_t shortAction = zoneShortAction(zone);
    if (shortAction == CrossPointSettings::TAP_ZONE_BOOKMARK ||
        shortAction == CrossPointSettings::TAP_ZONE_DICTIONARY) {
      result.bookmark = shortAction == CrossPointSettings::TAP_ZONE_BOOKMARK;
      result.dictionary = shortAction == CrossPointSettings::TAP_ZONE_DICTIONARY;
      return result;
    }
  }

  const uint8_t action = zoneShortAction(zone);
  switch (action) {
    case CrossPointSettings::TAP_ZONE_PREV:
      if (allowsTap(SETTINGS.previousPageGesture)) result.prev = true;
      break;
    case CrossPointSettings::TAP_ZONE_NEXT:
      if (allowsTap(SETTINGS.pageTurnGesture)) result.next = true;
      break;
    case CrossPointSettings::TAP_ZONE_MENU:
      // Consumed by isTouchMenuGesture (center-tap mode); never a page turn.
      break;
    case CrossPointSettings::TAP_ZONE_NONE:
      break;
    default:
      result.action = action;  // handed to the reader activity for dispatch
      break;
  }
  return result;
}

// Tap in the center third of the screen: the tap path into the reader menu on
// every touch board. detectTouchPageTurn() excludes this centered rectangle,
// so it remains free in tap mode. The Off/Swipe Up
// alternatives are only surfaced on home-key boards (SettingsList), where the
// menu stays reachable through the key's long-press function.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return false;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  return x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;
}

// Reader menu opens on the menu edge-swipe or a center-third tap. Home-key
// actions are configured separately from screen gestures.
// Menu gestures honor showReaderMenu independently of touchReaderControls,
// which only gates page-turn touch zones in detectTouchPageTurn().
inline bool isTouchMenuGesture(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (input.wasMenuGesture()) return true;
  // Bottom-edge up-swipe variant: only selectable on home-key boards, where
  // Home is the capacitive key and the bottom edge is otherwise unused.
  if (SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_SWIPE_UP && input.wasReaderMenuSwipeUp()) {
    return true;
  }
  return isTouchMenuTap(renderer, input);
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, ActivityManager& activityManager, RenderFn&& renderFn) {
  if (activityManager.isSwitchPending()) {
    renderer.cancelGrayscale();
    return;
  }
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.combinesGrayscaleBase()) {
      if (activityManager.isSwitchPending()) {
        renderer.cancelGrayscale();
      } else {
        renderer.cleanupGrayscaleWithFrameBuffer();
      }
    }
    return;
  }

  const auto cancelled = [&] {
    if (!activityManager.isSwitchPending()) return false;
    renderer.setRenderMode(GfxRenderer::BW);
    const bool combinedBase = renderer.combinesGrayscaleBase();
    if (combinedBase) renderer.cancelGrayscale();
    renderer.restoreBwBuffer(!combinedBase);
    return true;
  };
  if (cancelled()) return;
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  if (cancelled()) return;
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  if (cancelled()) return;
  renderer.copyGrayscaleMsbBuffers();

  if (cancelled()) return;
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

struct BackNavCallback {
  void* ctx;
  void (*fn)(void*);
};

// Returns true if the back button was consumed (caller should return).
// Long press (>= GO_BACK_OR_HOME_MS):
// - default: go to file browser
// - with backShortToFileBrowser: go home
// Short press (< GO_BACK_OR_HOME_MS):
// - default: go home
// - with backShortToFileBrowser: go to file browser.
inline bool handleBackNavigation(const MappedInputManager& mappedInput, ActivityManager& activityManager,
                                 const char* filePath, BackNavCallback goHome) {
  // The reading surface deliberately has no left-edge swipe-to-exit path: in
  // swipe page-turn mode a right swipe must page back instead. Home remains
  // available through the board's dedicated Home gesture/key. Back swipes stay
  // available in menus and other activities; only this reader-surface handler
  // ignores them. Physical Back buttons are unaffected: isPressed() is
  // button-only, and this guard skips just the gesture's own release frame.
  if (mappedInput.wasBackGesture()) {
    return false;
  }

  const bool backTriggered = mappedInput.wasLongPressed(MappedInputManager::Button::Back, GO_BACK_OR_HOME_MS) ||
                             mappedInput.wasReleased(MappedInputManager::Button::Back);
  if (!backTriggered) return false;

  const bool longPress = mappedInput.getHeldTime() >= GO_BACK_OR_HOME_MS;
  if (longPress != SETTINGS.backShortToFileBrowser) {
    activityManager.goToFileBrowser(filePath);
  } else {
    goHome.fn(goHome.ctx);
  }
  return true;
}

}  // namespace ReaderUtils
