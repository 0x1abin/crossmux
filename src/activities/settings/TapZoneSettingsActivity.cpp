#include "TapZoneSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool allowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY;
}

// Build the cycle list of zone actions for the current configuration:
// - PREV/NEXT are offered only while the matching direction still accepts
//   taps (a SWIPE_ONLY or disabled direction contributes no tap zone);
// - MENU only exists in center-tap menu mode;
// - BOOKMARK/DICTIONARY fire on a long press, never an ordinary tap.
// Writes at most max entries (callers pass the size of out) and returns the
// number written.
int buildZoneOptions(uint8_t* out, const int max) {
  int n = 0;
  if (n < max && allowsTap(SETTINGS.previousPageGesture)) out[n++] = CrossPointSettings::TAP_ZONE_PREV;
  if (n < max && allowsTap(SETTINGS.pageTurnGesture)) out[n++] = CrossPointSettings::TAP_ZONE_NEXT;
  if (n < max && SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP) {
    out[n++] = CrossPointSettings::TAP_ZONE_MENU;
  }
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_BOOKMARK;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_DICTIONARY;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_NONE;
  return n;
}

}  // namespace

void TapZoneSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedZone = 4;  // center zone
  requestUpdate();
}

void TapZoneSettingsActivity::loop() {
  const int hintH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int screenH = renderer.getScreenHeight();

  // Touch: a tap on a grid cell cycles that cell's action. The bottom button
  // hint row belongs to the hint buttons, so taps there never reach a cell.
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    if (tapY < screenH - hintH) {
      const ReaderUtils::TapZoneGrid grid(renderer.getScreenWidth(), screenH);
      const int zone = grid.zoneAt(tapX, tapY);
      if (zone >= 0) {
        cycleZone(static_cast<uint8_t>(zone));
      }
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveSelection(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveSelection(1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveSelection(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveSelection(0, 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    cycleSelected();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    finish();
    return;
  } else {
    return;
  }
  requestUpdate();
}

void TapZoneSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // Full-screen 3x3 grid: the only chrome is the bottom button hint row, which
  // is drawn over the last row afterwards. Cells are inset by a safe margin
  // and separated by a visible gap (see ReaderUtils::TapZoneGrid, shared with
  // the reader hit-testing), so the painted cells and the reader hit areas
  // cover exactly the same full display.
  const ReaderUtils::TapZoneGrid grid(renderer.getScreenWidth(), renderer.getScreenHeight());

  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      const uint8_t zone = static_cast<uint8_t>(row * 3 + col);
      const Rect cell = grid.cell(row, col);
      const bool selected = zone == selectedZone;
      // Selected cell is inverted (filled) so the focused zone is obvious on
      // an e-ink screen; its label draws white.
      renderer.fillRect(cell.x, cell.y, cell.width, cell.height, selected);
      renderer.drawRect(cell.x, cell.y, cell.width, cell.height, 1, true);

      const char* label = zoneLabel(SETTINGS.tapZones[zone]);
      const int textW = renderer.getTextWidth(UI_10_FONT_ID, label);
      const int textH = renderer.getLineHeight(UI_10_FONT_ID);
      renderer.drawText(UI_10_FONT_ID, cell.x + (cell.width - textW) / 2, cell.y + (cell.height - textH) / 2, label,
                        /*black=*/!selected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void TapZoneSettingsActivity::moveSelection(const int deltaX, const int deltaY) {
  const int col = static_cast<int>(selectedZone % 3) + deltaX;
  const int row = static_cast<int>(selectedZone / 3) + deltaY;
  if (col < 0 || col > 2 || row < 0 || row > 2) return;
  selectedZone = static_cast<uint8_t>(row * 3 + col);
}

void TapZoneSettingsActivity::cycleZone(const uint8_t zone) {
  uint8_t options[6];
  const int optionCount = buildZoneOptions(options, 6);

  const uint8_t current = SETTINGS.tapZones[zone];
  int idx = 0;
  for (int i = 0; i < optionCount; ++i) {
    if (options[i] == current) {
      idx = i;
      break;
    }
  }
  SETTINGS.tapZones[zone] = options[(idx + 1) % optionCount];
}

void TapZoneSettingsActivity::cycleSelected() { cycleZone(selectedZone); }

const char* TapZoneSettingsActivity::zoneLabel(const uint8_t action) const {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_PREV:
      return tr(STR_TAP_ZONE_PREV_PAGE);
    case CrossPointSettings::TAP_ZONE_NEXT:
      return tr(STR_TAP_ZONE_NEXT_PAGE);
    case CrossPointSettings::TAP_ZONE_MENU:
      return tr(STR_TAP_ZONE_MENU);
    case CrossPointSettings::TAP_ZONE_BOOKMARK:
      return tr(STR_TAP_ZONE_BOOKMARK);
    case CrossPointSettings::TAP_ZONE_DICTIONARY:
      return tr(STR_TAP_ZONE_DICTIONARY);
    default:
      return tr(STR_TAP_ZONE_NONE);
  }
}
