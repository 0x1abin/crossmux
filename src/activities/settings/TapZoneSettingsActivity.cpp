#include "TapZoneSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Byte length of one UTF-8 sequence starting at c (1..4), used for the
// vertical mini-zone labels and their glyph counting.
static int utf8SeqLen(const char* c) {
  const unsigned char ch = static_cast<unsigned char>(*c);
  if ((ch & 0xE0) == 0xC0) return 2;
  if ((ch & 0xF0) == 0xE0) return 3;
  if ((ch & 0xF8) == 0xF0) return 4;
  return 1;
}

// Short-press actions offered for the current configuration:
// - PREV/NEXT only while that direction's gesture still accepts taps (a
//   SWIPE_ONLY or disabled direction contributes no tap zone);
// - MENU only in center-tap menu mode (swipe-up mode never offers menu zones,
//   so the up-swipe keeps opening the menu on its own).
// Writes at most max entries and returns the number written.
int buildShortOptions(uint8_t* out, const int max) {
  int n = 0;
  // PREV/NEXT lead because they are the only options with no long-press twin;
  // everything after matches the long-press section's family order exactly, so
  // the two lists line up row for row.
  if (n < max && ReaderUtils::allowsTap(SETTINGS.previousPageGesture)) out[n++] = CrossPointSettings::TAP_ZONE_PREV;
  if (n < max && ReaderUtils::allowsTap(SETTINGS.pageTurnGesture)) out[n++] = CrossPointSettings::TAP_ZONE_NEXT;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_BOOKMARK;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_DICTIONARY;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_CHAPTER;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_FOOTNOTE;
  if (n < max && SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP) {
    out[n++] = CrossPointSettings::TAP_ZONE_MENU;
  }
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_ROTATE_CW;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_ROTATE_CCW;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_FRONTLIGHT;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_KOREADER;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_AUTO_TURN;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_JUMP_PERCENT;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_GO_HOME;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_REFRESH;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_NONE;
  return n;
}

// Long-press actions. PREV/NEXT have no long-press variant (a held tap on a
// page-turn zone keeps turning pages); MENU exists only in center-tap mode.
int buildLongOptions(uint8_t* out, const int max) {
  int n = 0;
  // Same family order as the short-press section (minus PREV/NEXT, which have
  // no long-press twin) so the two lists line up visually.
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_BOOKMARK;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_DICTIONARY;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_CHAPTER;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_FOOTNOTE;
  if (n < max && SETTINGS.showReaderMenu == CrossPointSettings::READER_MENU_TAP) {
    out[n++] = CrossPointSettings::TAP_ZONE_LONG_MENU;
  }
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_ROTATE_CW;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_ROTATE_CCW;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_FRONTLIGHT;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_KOREADER;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_AUTO_TURN;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_JUMP_PERCENT;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_GO_HOME;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_REFRESH;
  if (n < max) out[n++] = CrossPointSettings::TAP_ZONE_LONG_NONE;
  return n;
}

// Popup geometry: shared by drawing and touch hit-testing so the rendered rows
// and the tapped rows can never drift apart.
constexpr int kPopupTitleH = 24;
constexpr int kPopupRowH = 24;
constexpr int kPopupPad = 6;

}  // namespace

void TapZoneSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedZone = 4;  // center zone
  popupOpen = false;
  requestUpdate();
}

void TapZoneSettingsActivity::loop() {
  const int hintH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const int screenH = renderer.getScreenHeight();
  const int screenW = renderer.getScreenWidth();

  if (popupOpen) {
    // ---- Popup mode: the popup owns all input until dismissed ----
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
      int row = -1;
      if (popupHitRow(tapX, tapY, &row)) {
        if (row >= 0 && row < popupRowCount && !popupRows[row].isHeader) {
          setPopupAction(row);
          requestUpdate();
          return;
        }
      }
      closePopup();  // tap outside the popup cancels
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      popupCursor = stepOverHeaders(popupCursor, -1);
      requestUpdate();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      popupCursor = stepOverHeaders(popupCursor, 1);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      setPopupAction(popupCursor);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      closePopup();
      requestUpdate();
      return;
    }
    return;
  }

  // ---- Grid mode ----
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    // The grid ends above the bottom button-hint row, so hints never overlap a
    // painted cell and every painted cell is tappable (draw == hit).
    const ReaderUtils::TapZoneGrid grid(screenW, screenH - hintH);
    const int zone = grid.zoneAt(tapX, tapY);
    if (zone >= 0) {
      openPopup(static_cast<uint8_t>(zone));
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
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openPopup(selectedZone);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Exit on release so the parent activity's Back handling is never
    // triggered by the same press (see PR feedback on input ownership).
    SETTINGS.saveToFile();
    finish();
    return;
  } else {
    return;
  }
  requestUpdate();
}

// Breaks a label into at most kMaxLines lines that each fit within maxWidth.
// A space is a preferred break point; otherwise the string is split per
// character, so CJK labels also wrap instead of overflowing their cell.
static const int kLabelMaxLines = 3;
static int wrapLabel(const GfxRenderer& renderer, const int font, const char* text, const int maxWidth,
                     char lines[][32], const int maxLines) {
  int count = 0;
  const char* start = text;
  while (*start != '\0' && count < maxLines) {
    const char* p = start;
    const char* lastBreak = nullptr;
    while (*p != '\0') {
      const int n = static_cast<int>(p - start) + 1;
      char tmp[33];
      memcpy(tmp, start, n);
      tmp[n] = '\0';
      if (renderer.getTextWidth(font, tmp) > maxWidth) break;
      if (*p == ' ') lastBreak = p;
      ++p;
    }
    if (p == start) {
      ++p;  // a single character is wider than the cell: keep it anyway
    } else if (lastBreak != nullptr && lastBreak > start) {
      p = lastBreak;  // prefer breaking at a word boundary
    }
    const int n = static_cast<int>(p - start);
    memcpy(lines[count], start, static_cast<size_t>(n));
    lines[count][n] = '\0';
    ++count;
    start = p;
    if (*start == ' ') ++start;  // skip the space we broke at
  }
  return count;
}

void TapZoneSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  // 15-zone grid ending above the bottom button-hint row: cells use exactly
  // the rectangles the hit-test uses here and the reader uses (same
  // ReaderUtils::TapZoneGrid safe margin, visible gaps and integer sizes), so
  // painted zones and tap areas agree and hints never cover a cell.
  const int hintH = UITheme::getInstance().getMetrics().buttonHintsHeight;
  const ReaderUtils::TapZoneGrid grid(renderer.getScreenWidth(), renderer.getScreenHeight() - hintH);

  // Main 3x3 grid, dashed outlines.
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      const uint8_t zone = static_cast<uint8_t>(row * 3 + col);
      const Rect cell = grid.cell(row, col);
      const bool selected = zone == selectedZone && !popupOpen;
      if (selected) {
        renderer.fillRectDither(cell.x, cell.y, cell.width, cell.height, Color::LightGray);
      }
      drawDashedRect(cell.x, cell.y, cell.width, cell.height);

      // "短按 <action>" then "长按 <action>". A line whose action is NONE is
      // hidden, so a cell with only one kind of action shows a single line;
      // with both unset it shows "无". Long action names wrap instead of
      // overflowing.
      const uint8_t shortAction = ReaderUtils::zoneShortAction(zone);
      const uint8_t longAction = ReaderUtils::zoneLongAction(zone);
      const int textH = renderer.getLineHeight(UI_10_FONT_ID);
      const bool hasShort = shortAction != CrossPointSettings::TAP_ZONE_NONE;
      const bool hasLong = longAction != CrossPointSettings::TAP_ZONE_LONG_NONE;
      const int maxW = cell.width - 12;
      char shortLine[32];
      snprintf(shortLine, sizeof(shortLine), "%s %s", tr(STR_TAP_ZONE_SHORT), zoneLabel(shortAction));
      char shortLines[kLabelMaxLines][32];
      const int shortRows =
          hasShort ? wrapLabel(renderer, UI_10_FONT_ID, shortLine, maxW, shortLines, kLabelMaxLines) : 0;
      char longLine[32];
      snprintf(longLine, sizeof(longLine), "%s %s", tr(STR_TAP_ZONE_LONG), zoneLongLabel(longAction));
      char longLines[kLabelMaxLines][32];
      const int longRows = hasLong ? wrapLabel(renderer, UI_10_FONT_ID, longLine, maxW, longLines, kLabelMaxLines) : 0;
      const int totalRows = shortRows + longRows;
      if (totalRows == 0) {
        const char* none = zoneLabel(CrossPointSettings::TAP_ZONE_NONE);
        const int noneW = renderer.getTextWidth(UI_10_FONT_ID, none);
        renderer.drawText(UI_10_FONT_ID, cell.x + (cell.width - noneW) / 2, cell.y + (cell.height - textH) / 2, none,
                          true);
      } else {
        const int blockY = cell.y + (cell.height - textH * totalRows) / 2;
        for (int i = 0; i < shortRows; ++i) {
          const int w = renderer.getTextWidth(UI_10_FONT_ID, shortLines[i]);
          renderer.drawText(UI_10_FONT_ID, cell.x + (cell.width - w) / 2, blockY + textH * i, shortLines[i], true);
        }
        for (int i = 0; i < longRows; ++i) {
          const int w = renderer.getTextWidth(UI_10_FONT_ID, longLines[i]);
          renderer.drawText(UI_10_FONT_ID, cell.x + (cell.width - w) / 2, blockY + textH * (shortRows + i),
                            longLines[i], true);
        }
      }
    }
  }

  // Six small corner/edge zones, solid outlines (no dashed lines).
  for (int i = 0; i < ReaderUtils::TapZoneGrid::kMiniCount; ++i) {
    const uint8_t zone = static_cast<uint8_t>(9 + i);
    const Rect cell = grid.miniCell(i);
    const bool selected = zone == selectedZone && !popupOpen;
    if (selected) {
      renderer.fillRectDither(cell.x, cell.y, cell.width, cell.height, Color::LightGray);
    }
    renderer.drawRect(cell.x, cell.y, cell.width, cell.height, 1, true);

    // Mini zones are tap-only: show the user's own choice (raw, no main-cell
    // fallback) as a vertical two-character label with the compact SMALL font.
    // NONE renders as "无" even though taps still inherit the main cell.
    const uint8_t shortAction = ReaderUtils::zoneRawShortAction(zone);
    drawMiniLabel(cell, zoneShortName(shortAction));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (popupOpen) renderPopup();
  renderer.displayBuffer();
}

void TapZoneSettingsActivity::moveSelection(const int deltaX, const int deltaY) {
  // D-pad cursor walks the main 3x3 grid only.
  const int col = static_cast<int>(selectedZone % 3) + deltaX;
  const int row = static_cast<int>(selectedZone / 3) + deltaY;
  if (col < 0 || col > 2 || row < 0 || row > 2) return;
  selectedZone = static_cast<uint8_t>(row * 3 + col);
}

int TapZoneSettingsActivity::stepOverHeaders(int index, const int delta) const {
  if (popupRowCount <= 0) return index;
  int next = index;
  do {
    next += delta;
    if (next < 0) next = popupRowCount - 1;
    if (next >= popupRowCount) next = 0;
  } while (popupRows[next].isHeader);
  return next;
}

void TapZoneSettingsActivity::openPopup(const uint8_t zone) {
  popupZone = zone;
  popupPhase = 0;
  rebuildPopupRows();
  popupOpen = true;
}

// Two-step picker: the popup first shows the short-press section; selecting an
// action advances to the long-press section (main 3x3 grid only), then closes.
// Mini zones are tap-only and close right after the short-press pick.
void TapZoneSettingsActivity::rebuildPopupRows() {
  popupRowCount = 0;
  int n = 0;
  if (popupPhase == 0) {
    const uint8_t shortAction = ReaderUtils::zoneRawShortAction(popupZone);
    popupRows[n] = {tr(STR_TAP_ZONE_SHORT), 0, true};
    n++;
    uint8_t shortOptions[17];
    const int shortCount = buildShortOptions(shortOptions, 17);
    for (int i = 0; i < shortCount && n < 32; ++i) {
      popupRows[n] = {zoneLabel(shortOptions[i]), shortOptions[i], false};
      n++;
    }
    popupRowCount = n;
    // Cursor starts on the row matching the current short action.
    popupCursor = 1;
    for (int i = 1; i < n; ++i) {
      if (popupRows[i].isHeader) continue;
      if (popupRows[i].value == shortAction) {
        popupCursor = i;
        break;
      }
    }
  } else {
    const uint8_t longAction = ReaderUtils::zoneLongAction(popupZone);
    popupRows[n] = {tr(STR_TAP_ZONE_LONG), 0, true};
    n++;
    uint8_t longOptions[15];
    const int longCount = buildLongOptions(longOptions, 15);
    for (int i = 0; i < longCount && n < 32; ++i) {
      popupRows[n] = {zoneLongLabel(longOptions[i]), longOptions[i], false};
      n++;
    }
    popupRowCount = n;
    // Cursor starts on the row matching the current long action.
    popupCursor = 1;
    for (int i = 1; i < n; ++i) {
      if (popupRows[i].isHeader) continue;
      if (popupRows[i].value == longAction) {
        popupCursor = i;
        break;
      }
    }
  }
}

void TapZoneSettingsActivity::closePopup() {
  popupOpen = false;
  popupRowCount = 0;
}

void TapZoneSettingsActivity::setPopupAction(const int row) {
  if (row < 0 || row >= popupRowCount || popupRows[row].isHeader) return;
  const uint8_t value = popupRows[row].value;
  const bool isShort = popupPhase == 0;
  if (popupZone < 9) {
    if (isShort) {
      SETTINGS.tapZones[popupZone] = value;
    } else {
      SETTINGS.tapZonesLong[popupZone] = value;
    }
  } else {
    // Mini zones are tap-only; only the short-press section is ever shown.
    if (isShort) SETTINGS.miniZones[popupZone - 9] = value;
  }
  // Two-step flow: after the short-press pick on the main grid advance to the
  // long-press section; otherwise the pick is done and the popup closes.
  if (isShort && popupZone < 9) {
    popupPhase = 1;
    rebuildPopupRows();
  } else {
    closePopup();
  }
}

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
    case CrossPointSettings::TAP_ZONE_ROTATE_CW:
      return tr(STR_TAP_ZONE_ROTATE_CW);
    case CrossPointSettings::TAP_ZONE_ROTATE_CCW:
      return tr(STR_TAP_ZONE_ROTATE_CCW);
    case CrossPointSettings::TAP_ZONE_FRONTLIGHT:
      return tr(STR_TAP_ZONE_FRONTLIGHT);
    case CrossPointSettings::TAP_ZONE_KOREADER:
      return tr(STR_TAP_ZONE_KOREADER);
    case CrossPointSettings::TAP_ZONE_AUTO_TURN:
      return tr(STR_TAP_ZONE_AUTO_TURN);
    case CrossPointSettings::TAP_ZONE_JUMP_PERCENT:
      return tr(STR_GO_TO_PERCENT);  // same wording as the reading-menu jump-to-%
    case CrossPointSettings::TAP_ZONE_GO_HOME:
      return tr(STR_TAP_ZONE_GO_HOME);
    case CrossPointSettings::TAP_ZONE_CHAPTER:
      return tr(STR_TAP_ZONE_CHAPTER);
    case CrossPointSettings::TAP_ZONE_FOOTNOTE:
      return tr(STR_TAP_ZONE_FOOTNOTE);
    case CrossPointSettings::TAP_ZONE_REFRESH:
      return tr(STR_TAP_ZONE_REFRESH);
    default:
      return tr(STR_TAP_ZONE_NONE);
  }
}

const char* TapZoneSettingsActivity::zoneLongLabel(const uint8_t action) const {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_LONG_BOOKMARK:
      return tr(STR_TAP_ZONE_BOOKMARK);
    case CrossPointSettings::TAP_ZONE_LONG_DICTIONARY:
      return tr(STR_TAP_ZONE_DICTIONARY);
    case CrossPointSettings::TAP_ZONE_LONG_CHAPTER:
      return tr(STR_TAP_ZONE_CHAPTER);
    case CrossPointSettings::TAP_ZONE_LONG_FOOTNOTE:
      return tr(STR_TAP_ZONE_FOOTNOTE);
    case CrossPointSettings::TAP_ZONE_LONG_MENU:
      return tr(STR_TAP_ZONE_MENU);
    case CrossPointSettings::TAP_ZONE_LONG_ROTATE_CW:
      return tr(STR_TAP_ZONE_ROTATE_CW);
    case CrossPointSettings::TAP_ZONE_LONG_ROTATE_CCW:
      return tr(STR_TAP_ZONE_ROTATE_CCW);
    case CrossPointSettings::TAP_ZONE_LONG_FRONTLIGHT:
      return tr(STR_TAP_ZONE_FRONTLIGHT);
    case CrossPointSettings::TAP_ZONE_LONG_KOREADER:
      return tr(STR_TAP_ZONE_KOREADER);
    case CrossPointSettings::TAP_ZONE_LONG_AUTO_TURN:
      return tr(STR_TAP_ZONE_AUTO_TURN);
    case CrossPointSettings::TAP_ZONE_LONG_JUMP_PERCENT:
      return tr(STR_GO_TO_PERCENT);  // same wording as the reading-menu jump-to-%
    case CrossPointSettings::TAP_ZONE_LONG_GO_HOME:
      return tr(STR_TAP_ZONE_GO_HOME);
    case CrossPointSettings::TAP_ZONE_LONG_REFRESH:
      return tr(STR_TAP_ZONE_REFRESH);
    default:
      return tr(STR_TAP_ZONE_NONE);
  }
}

const char* TapZoneSettingsActivity::zoneShortName(const uint8_t action) const {
  switch (action) {
    case CrossPointSettings::TAP_ZONE_PREV:
      return tr(STR_TAP_ZONE_S_PREV);
    case CrossPointSettings::TAP_ZONE_NEXT:
      return tr(STR_TAP_ZONE_S_NEXT);
    case CrossPointSettings::TAP_ZONE_MENU:
      return tr(STR_TAP_ZONE_S_MENU);
    case CrossPointSettings::TAP_ZONE_BOOKMARK:
      return tr(STR_TAP_ZONE_S_BOOKMARK);
    case CrossPointSettings::TAP_ZONE_DICTIONARY:
      return tr(STR_TAP_ZONE_S_DICTIONARY);
    case CrossPointSettings::TAP_ZONE_ROTATE_CW:
      return tr(STR_TAP_ZONE_S_ROTATE_CW);
    case CrossPointSettings::TAP_ZONE_ROTATE_CCW:
      return tr(STR_TAP_ZONE_S_ROTATE_CCW);
    case CrossPointSettings::TAP_ZONE_FRONTLIGHT:
      return tr(STR_TAP_ZONE_S_FRONTLIGHT);
    case CrossPointSettings::TAP_ZONE_KOREADER:
      return tr(STR_TAP_ZONE_S_KOREADER);
    case CrossPointSettings::TAP_ZONE_AUTO_TURN:
      return tr(STR_TAP_ZONE_S_AUTO_TURN);
    case CrossPointSettings::TAP_ZONE_JUMP_PERCENT:
      return tr(STR_TAP_ZONE_S_JUMP_PERCENT);
    case CrossPointSettings::TAP_ZONE_GO_HOME:
      return tr(STR_TAP_ZONE_S_GO_HOME);
    case CrossPointSettings::TAP_ZONE_CHAPTER:
      return tr(STR_TAP_ZONE_S_CHAPTER);
    case CrossPointSettings::TAP_ZONE_FOOTNOTE:
      return tr(STR_TAP_ZONE_S_FOOTNOTE);
    case CrossPointSettings::TAP_ZONE_REFRESH:
      return tr(STR_TAP_ZONE_S_REFRESH);
    default:
      return tr(STR_TAP_ZONE_S_NONE);
  }
}

void TapZoneSettingsActivity::drawMiniLabel(const Rect& cell, const char* label) const {
  if (label == nullptr) return;
  const int textH = renderer.getLineHeight(SMALL_FONT_ID);
  if (cell.width > cell.height) {
    // Wide (top/bottom edge-middle) cells render the label horizontally: at
    // most two code points side by side, centered in the strip.
    char buf[11] = {0};
    int len = 0;
    int drawn = 0;
    for (const char* c = label; *c != '\0' && drawn < 2; c += utf8SeqLen(c), ++drawn) {
      const int sl = utf8SeqLen(c);
      std::memcpy(buf + len, c, static_cast<size_t>(sl));
      len += sl;
    }
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, buf);
    renderer.drawText(SMALL_FONT_ID, cell.x + (cell.width - textW) / 2, cell.y + (cell.height - textH) / 2, buf, true);
    return;
  }
  // Vertical rendering of the short action's label: one UTF-8 code point per
  // line (at most two), centered with SMALL_FONT so it always fits the cell.
  int glyphs = 0;
  for (const char* c = label; *c != '\0'; c += utf8SeqLen(c)) {
    glyphs++;
  }
  if (glyphs > 2) glyphs = 2;
  int cy = cell.y + (cell.height - textH * glyphs) / 2;
  const char* c = label;
  int drawn = 0;
  while (*c != '\0' && drawn < glyphs) {
    const int len = utf8SeqLen(c);
    char buf[5] = {0};
    std::memcpy(buf, c, static_cast<size_t>(len));
    const int charW = renderer.getTextWidth(SMALL_FONT_ID, buf);
    renderer.drawText(SMALL_FONT_ID, cell.x + (cell.width - charW) / 2, cy, buf, true);
    c += len;
    cy += textH;
    drawn++;
  }
}

void TapZoneSettingsActivity::drawDashedRect(const int x, const int y, const int w, const int h) const {
  constexpr int kDash = 3;
  constexpr int kGap = 2;
  for (int px = x; px < x + w; px += kDash + kGap) {
    renderer.drawLine(px, y, (px + kDash - 1) < x + w - 1 ? px + kDash - 1 : x + w - 1, y, 1, true);
    renderer.drawLine(px, y + h - 1, (px + kDash - 1) < x + w - 1 ? px + kDash - 1 : x + w - 1, y + h - 1, 1, true);
  }
  for (int py = y; py < y + h; py += kDash + kGap) {
    renderer.drawLine(x, py, x, (py + kDash - 1) < y + h - 1 ? py + kDash - 1 : y + h - 1, 1, true);
    renderer.drawLine(x + w - 1, py, x + w - 1, (py + kDash - 1) < y + h - 1 ? py + kDash - 1 : y + h - 1, 1, true);
  }
}

// Dotted horizontal rule used between dialog rows, matching the standard INX
// option dialog's row separators.
static void drawDottedHRule(const GfxRenderer& renderer, const int x, const int y, const int w) {
  for (int i = 0; i < w; i += 4) {
    renderer.drawLine(x + i, y, (x + i + 1) < x + w - 1 ? x + i + 1 : x + w - 1, y, 1, true);
  }
}

void TapZoneSettingsActivity::renderPopup() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  // Keep the popup inside the display, centered on the grid.
  const int panelW = (screenW - 20) < 300 ? (screenW - 20) : 300;
  const int panelH = kPopupTitleH + popupRowCount * kPopupRowH + kPopupPad * 2;
  const int px = (screenW - panelW) / 2;
  const int py = (screenH - panelH) / 2;

  // Flat white panel with a double black outline, exactly like the standard
  // INX option dialog (no rounded corners, no decorative bands).
  renderer.fillRect(px, py, panelW, panelH, false);
  renderer.drawRect(px, py, panelW, panelH, true);
  renderer.drawRect(px + 1, py + 1, panelW - 2, panelH - 2, true);

  // Title: bold, left-aligned, under a full-width rule.
  char title[24];
  snprintf(title, sizeof(title), "Zone %d · %s", popupZone,
           popupPhase == 0 ? tr(STR_TAP_ZONE_SHORT) : tr(STR_TAP_ZONE_LONG));
  renderer.drawText(UI_10_FONT_ID, px + 16, py + (kPopupTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title,
                    true, EpdFontFamily::BOLD);
  renderer.drawLine(px, py + kPopupTitleH - 1, px + panelW - 1, py + kPopupTitleH - 1, 1, true);

  const uint8_t shortAction = ReaderUtils::zoneRawShortAction(popupZone);
  const uint8_t longAction = ReaderUtils::zoneLongAction(popupZone);
  const int rowY = py + kPopupTitleH;
  const int textH = renderer.getLineHeight(UI_10_FONT_ID);

  for (int i = 0; i < popupRowCount; ++i) {
    const int ry = rowY + i * kPopupRowH;
    const bool isShort = popupPhase == 0;
    const bool isCurrent =
        !popupRows[i].isHeader && (isShort ? popupRows[i].value == shortAction : popupRows[i].value == longAction);
    const bool isCursor = i == popupCursor;

    if (popupRows[i].isHeader) {
      // Section header: plain bold text, no background band.
      renderer.drawText(UI_10_FONT_ID, px + 18, ry + (kPopupRowH - textH) / 2, popupRows[i].label, true,
                        EpdFontFamily::BOLD);
    } else if (isCursor || isCurrent) {
      // Selected/cursor row: full-row invert with white bold text, matching
      // the standard INX dialog. A cursor row that is not the current value
      // keeps a black focus ring around the invert band.
      renderer.fillRect(px + 1, ry + 1, panelW - 2, kPopupRowH - 2, true);
      renderer.drawText(UI_10_FONT_ID, px + 18, ry + (kPopupRowH - textH) / 2, popupRows[i].label, false,
                        EpdFontFamily::BOLD);
      if (isCursor && !isCurrent) {
        renderer.drawRect(px + 1, ry + 1, panelW - 2, kPopupRowH - 2, true);
      }
    } else {
      renderer.drawText(UI_10_FONT_ID, px + 18, ry + (kPopupRowH - textH) / 2, popupRows[i].label, true);
    }
    if (i + 1 < popupRowCount) {
      drawDottedHRule(renderer, px + 2, ry + kPopupRowH - 1, panelW - 4);
    }
  }
}

bool TapZoneSettingsActivity::popupHitRow(const int x, const int y, int* outRow) const {
  if (!popupOpen) return false;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int panelW = (screenW - 20) < 300 ? (screenW - 20) : 300;
  const int panelH = kPopupTitleH + popupRowCount * kPopupRowH + kPopupPad * 2;
  const int px = (screenW - panelW) / 2;
  const int py = (screenH - panelH) / 2;
  if (x < px || x >= px + panelW || y < py || y >= py + panelH) return false;
  const int ry = y - (py + kPopupTitleH);
  if (ry < 0 || ry >= popupRowCount * kPopupRowH) return false;
  *outRow = ry / kPopupRowH;
  return true;
}
