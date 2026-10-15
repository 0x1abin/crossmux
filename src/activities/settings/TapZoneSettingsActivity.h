#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Edits the reader's tap-zone grid: the 3x3 main zones plus six small
// corner/edge zones (15 zones total). Main zones pair a short-press action
// with an optional long-press action; mini zones are tap-only. A tap on a zone
// opens the shared option dialog with the short-press actions (and the
// long-press section on main zones) listed for the current configuration; both
// touch and the D-pad work inside the dialog, exactly like every other
// settings picker.
//
// The painted cells are exactly the rectangles ReaderUtils::TapZoneGrid uses
// for reader hit-testing (same safe margin, visible gaps and integer cell
// sizes), so what the editor shows is what the reader hits; the main grid is
// outlined with dashed lines and the small zones with solid ones.
class TapZoneSettingsActivity final : public Activity {
 public:
  explicit TapZoneSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TapZones", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // D-pad cursor: main zones only (0..8); the small zones are set by touch.
  uint8_t selectedZone = 4;
  uint8_t popupZone = 0;  // zone (0..14) being edited
  int popupPhase = 0;     // 0 = short-press section, 1 = long-press section (two-step picker)
  // The shared option dialog reused by every other settings picker; the
  // short-press → long-press flow is two consecutive show() calls.
  OptionPopup popup_;
  // Actions offered by the currently shown section, indexed by the dialog row.
  uint8_t popupOptions_[17] = {};
  int popupOptionCount_ = 0;

  void moveSelection(int deltaX, int deltaY);
  void openPopup(uint8_t zone);
  void showPopupSection();
  void onPopupPicked(int index);
  const char* zoneLabel(uint8_t action) const;
  const char* zoneLongLabel(uint8_t action) const;
  const char* zoneShortName(uint8_t action) const;  // two-char short label (short-press space)
  void drawMiniLabel(const Rect& cell, const char* label) const;
  void drawDashedRect(int x, int y, int w, int h) const;
};
