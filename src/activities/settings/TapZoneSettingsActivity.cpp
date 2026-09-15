#include "TapZoneSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool allowsTap(const uint8_t gesture) {
  return gesture == CrossPointSettings::TAP_AND_SWIPE || gesture == CrossPointSettings::TAP_ONLY;
}

// Cell geometry for the 3x3 zone grid, used by both rendering and hit-testing
// so a tap can never land on a different cell than the one that is painted at
// non-divisible screen sizes. The grid is inset by a safe margin from the
// display edge and the cells are separated by a visible gap.
struct ZoneGrid {
  static constexpr int kSafeMargin = 6;  // inset from the grid area edges
  static constexpr int kGap = 6;         // visible gap between cells

  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int cellWidth = 0;
  int cellHeight = 0;

  explicit ZoneGrid(const int gridW, const int gridH) {
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
  // outside the grid. Shares the exact painted rectangles with cell().
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
  const int gridW = renderer.getScreenWidth();
  const int gridH = renderer.getScreenHeight() - hintH;

  // Touch: a tap on a grid cell cycles that cell's action.
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    const ZoneGrid grid(gridW, gridH);
    const int zone = grid.zoneAt(tapX, tapY);
    if (zone >= 0) {
      cycleZone(static_cast<uint8_t>(zone));
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

  // Full-screen 3x3 grid: the only chrome is the bottom button hint row, so
  // the tap zones cover as much of the display as possible. Cells are inset
  // by a safe margin and separated by a visible gap (see ZoneGrid).
  const int hintH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int gridW = renderer.getScreenWidth();
  const int gridH = renderer.getScreenHeight() - hintH;
  const ZoneGrid grid(gridW, gridH);

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
