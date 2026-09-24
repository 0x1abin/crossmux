#pragma once

#include "activities/Activity.h"

// Edits the reader's 3x3 tap-zone grid. Each zone cycles through the actions
// offered for the current menu mode: center-tap mode offers Previous / Next /
// Open Menu / Bookmark / Dictionary / None, swipe-up mode drops the Open Menu
// action, and a direction whose gesture is swipe-only is omitted entirely.
// Bookmark and Dictionary fire on a long press, never an ordinary tap.
class TapZoneSettingsActivity final : public Activity {
 public:
  explicit TapZoneSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TapZones", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  uint8_t selectedZone = 4;  // start on the center zone

  void moveSelection(int deltaX, int deltaY);
  void cycleZone(uint8_t zone);
  void cycleSelected();
  const char* zoneLabel(uint8_t action) const;
};
