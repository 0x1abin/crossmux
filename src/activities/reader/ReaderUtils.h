#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "ReaderRefresh.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"

namespace ReaderUtils {

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long GO_BACK_OR_HOME_MS = GO_HOME_MS;
constexpr unsigned long SKIP_HOLD_MS = 700;
constexpr unsigned long BOOKMARK_HOLD_MS = 400;
constexpr unsigned long BOOKMARK_MESSAGE_DURATION_MS = 2500;

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
  const bool next = tiltNext || pageButtonTriggered(MappedInputManager::Button::PageForward) || powerTurn ||
                    pageButtonTriggered(nextButton);
  return {prev, next, tiltPrev || tiltNext};
}

struct TouchPageTurn {
  bool prev;
  bool next;
  bool bookmark;
  bool dictionary;
  unsigned long heldMs;
};

// Shared 3x3 tap-zone grid geometry. The reader hit-tests with the exact
// rectangles the zone editor paints — same safe margin, visible gaps and cell
// sizes — so a tap in the reader lands on the zone the editor showed, and a
// tap in a gap or at the display edge is ignored instead of snapping to a
// neighbouring cell at non-divisible sizes.
struct TapZoneGrid {
  static constexpr int kSafeMargin = 6;  // inset from the grid area edges
  static constexpr int kGap = 6;         // visible gap between cells

  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int cellWidth = 0;
  int cellHeight = 0;

  explicit TapZoneGrid(const int gridW, const int gridH) {
    x = kSafeMargin;
    y = kSafeMargin;
    width = gridW - kSafeMargin * 2;
    height = gridH - kSafeMargin * 2;
    // Two internal gaps per axis; the remainder (if the size is not exactly
    // divisible) widens the outer gaps, never the visible cells.
    cellWidth = (width - kGap * 2) / 3;
    cellHeight = (height - kGap * 2) / 3;
  }

  Rect cell(const int row, const int col) const {
    return Rect{static_cast<int16_t>(x + col * (cellWidth + kGap)), static_cast<int16_t>(y + row * (cellHeight + kGap)),
                static_cast<int16_t>(cellWidth), static_cast<int16_t>(cellHeight)};
  }

  // Zone index (row-major) for a point, or -1 when it lands in a gap or
  // outside the grid.
  int zoneAt(const int px, const int py) const {
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

// Action of the reader tap zone at the given screen point. The screen is
// split into the same full-screen 3x3 grid the zone editor paints: inset by
// the safe margin, separated by visible gaps. A tap in a gap or in the safe
// margin falls through to TAP_ZONE_NONE instead of snapping to a neighbouring
// cell. The reading surface has no bottom button-hint row, so the grid covers
// the full display exactly like the original outer-thirds zones did.
inline uint8_t tapZoneAction(const GfxRenderer& renderer, const int x, const int y) {
  const int16_t width = static_cast<int16_t>(renderer.getScreenWidth());
  const int16_t height = static_cast<int16_t>(renderer.getScreenHeight());
  if (width <= 0 || height <= 0) return CrossPointSettings::TAP_ZONE_NONE;
  const TapZoneGrid grid(width, height);
  const int zone = grid.zoneAt(x, y);
  if (zone < 0) return CrossPointSettings::TAP_ZONE_NONE;
  return SETTINGS.tapZones[zone];
}

inline TouchPageTurn detectTouchPageTurn(const GfxRenderer& renderer, const MappedInputManager& input) {
  TouchPageTurn result{false, false, false, false, 0};
  if (!SETTINGS.touchReaderControls || !input.hasTouch()) {
    return result;
  }

  const auto allowsSwipe = [](const uint8_t gesture) {
    return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::SWIPE_ONLY;
  };
  // A direction whose gesture does not accept taps (SWIPE_ONLY or disabled)
  // contributes no tap zone: its marked zones fall through, as configured.
  const auto allowsTap = [](const uint8_t gesture) {
    return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY;
  };

  // Long-press on a BOOKMARK/DICTIONARY zone fires when the finger lifts after
  // being held still (within tap slop) for BOOKMARK_HOLD_MS — the action
  // happens on release, never while the finger is still down. PREV/NEXT zones
  // (or unconfigured spots) keep their plain tap behavior on lift regardless of
  // hold duration, so the original page-turn / chapter-skip behavior is
  // unchanged.

  // Horizontal swipes follow the per-direction gesture configuration. The
  // reader menu owns the vertical swipes, never page turns.
  const auto swipe = input.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right) {
    result.next = swipe == MappedInputManager::SwipeDir::Left && allowsSwipe(SETTINGS.pageTurnGesture);
    result.prev = swipe == MappedInputManager::SwipeDir::Right && allowsSwipe(SETTINGS.previousPageGesture);
    return result;
  }

  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) {
    return result;
  }

  // 3x3 tap-zone lookup, evaluated on release. A BOOKMARK/DICTIONARY zone only
  // fires when the contact was held for BOOKMARK_HOLD_MS (the SDK latches the
  // contact duration at release); a quick tap on those zones does nothing. A
  // zone marked for a direction only acts when that direction's gesture accepts
  // taps; MENU zones are consumed by isTouchMenuGesture and never turn pages
  // here.
  const uint8_t action = tapZoneAction(renderer, x, y);
  result.heldMs = gpio.lastTouchHeldMs();
  if (result.heldMs >= BOOKMARK_HOLD_MS &&
      (action == CrossPointSettings::TAP_ZONE_BOOKMARK || action == CrossPointSettings::TAP_ZONE_DICTIONARY)) {
    result.bookmark = action == CrossPointSettings::TAP_ZONE_BOOKMARK;
    result.dictionary = action == CrossPointSettings::TAP_ZONE_DICTIONARY;
    return result;
  }
  if (action == CrossPointSettings::TAP_ZONE_PREV && allowsTap(SETTINGS.previousPageGesture)) {
    result.prev = true;
  } else if (action == CrossPointSettings::TAP_ZONE_NEXT && allowsTap(SETTINGS.pageTurnGesture)) {
    result.next = true;
  }
  return result;
}

// The reader menu opens on a tap in a MENU-marked zone (center-tap mode), the
// board's existing edge menu gesture, or — in swipe-up mode — the original
// bottom-edge upward swipe.
inline bool isTouchMenuTap(const GfxRenderer& renderer, const MappedInputManager& input) {
  if (!input.hasTouch()) return false;
  if (SETTINGS.showReaderMenu != CrossPointSettings::READER_MENU_TAP) return false;
  int x = 0;
  int y = 0;
  if (!input.wasScreenTapped(x, y)) return false;
  return tapZoneAction(renderer, x, y) == CrossPointSettings::TAP_ZONE_MENU;
}

// Reader menu opens on the menu edge-swipe or a tap in a MENU-marked zone. On
// home-key boards a long press of the capacitive key runs the user-selected
// long-press function instead (SETTINGS.longPressMenuFunction), not the menu.
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
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    // A combined-base panel may still hold a deferred B/W activation; flush it
    // so the page reaches the panel even without its grays.
    if (renderer.combinesGrayscaleBase()) renderer.cleanupGrayscaleWithFrameBuffer();
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

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
