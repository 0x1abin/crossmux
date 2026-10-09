#pragma once

#include "activities/Activity.h"

// Edits the reader's tap-zone grid: the 3x3 main zones plus six small
// corner/edge zones (15 zones total). Main zones pair a short-press action
// with an optional long-press action; mini zones are tap-only. A tap on a zone
// opens a popup with a short-press section (and a long-press section on main
// zones) listing the actions available for the current configuration; both
// touch and the D-pad work inside the popup.
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
  struct PopupRow {
    const char* label = nullptr;
    uint8_t value = 0;
    bool isHeader = false;
  };

  // D-pad cursor: main zones only (0..8); the small zones are set by touch.
  uint8_t selectedZone = 4;
  bool popupOpen = false;
  uint8_t popupZone = 0;  // zone (0..14) being edited
  int popupCursor = 0;    // linear index into popupRows (action rows only)
  int popupRowCount = 0;  // rows currently populated
  int popupPhase = 0;     // 0 = short-press section, 1 = long-press section (two-step picker)
  PopupRow popupRows[32];

  void moveSelection(int deltaX, int deltaY);
  void openPopup(uint8_t zone);
  void rebuildPopupRows();
  void closePopup();
  void setPopupAction(int row);
  int stepOverHeaders(int index, int delta) const;
  const char* zoneLabel(uint8_t action) const;
  const char* zoneLongLabel(uint8_t action) const;
  const char* zoneShortName(uint8_t action) const;  // two-char short label (short-press space)
  void drawMiniLabel(const Rect& cell, const char* label) const;
  void drawDashedRect(int x, int y, int w, int h) const;
  void renderPopup();
  bool popupHitRow(int x, int y, int* outRow) const;
};
