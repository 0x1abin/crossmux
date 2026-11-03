#!/usr/bin/env python3
"""Tap-zone geometry: the editor must hit-test exactly what the reader hits.

The editor paints a 15-zone grid and lets a tap configure the zone under it.
If it builds the grid from a shorter area than the reader does, the zone a tap
configures is not the zone it drew. The mislabelled bands are large: with a
480x800 panel and a 40px hint row subtracted, the second and third grid rows
were off by a whole cell and (400,515) configured zone 8 in the editor while
the reader turned the page of zone 5.

Boards without a touch controller still reach this screen (it is a button
editor there: D-pad moves, Confirm opens the option dialog), and those boards
really do paint the bottom hint row, so the editor has to refuse taps under it
while keeping the reader's grid for every other pixel.

This compiles the production ReaderUtils::TapZoneGrid and runs it once per call
site, so a future change to either side's inputs fails here, not on a device.
"""
import os
import re
from pathlib import Path
import tempfile

from test_theme_menus import read, run

# How each side builds the grid. The reader must use the full display (the
# reading surface has no hint row); the editor must match it.
READER_GRID = re.compile(r'TapZoneGrid\(\s*(width,\s*height|gridW,\s*gridH)\s*\)')
# The editor declares `const ReaderUtils::TapZoneGrid grid(...)`, so the type is
# followed by the variable name before the arguments.
EDITOR_GRID = re.compile(r'(?:ReaderUtils::)?TapZoneGrid\s+\w+\(\s*'
                         r'(?:screenW|renderer\.getScreenWidth\(\))\s*,\s*'
                         r'(renderer\.getScreenHeight\(\)|screenH|screenH\s*-\s*hintH)\s*\)')


def geometry(directory):
    utils = read('src/activities/reader/ReaderUtils.h')
    grid = re.search(r'struct TapZoneGrid \{.*?\n\};', utils, re.S).group()
    activity = read('src/activities/settings/TapZoneSettingsActivity.cpp')

    reader_sites = READER_GRID.findall(utils)
    assert reader_sites, 'reader no longer builds TapZoneGrid from the full display'
    editor_sites = EDITOR_GRID.findall(activity)
    assert len(editor_sites) == 1, f'expected exactly one grid in the zone editor, found {editor_sites}'
    for site in editor_sites:
        assert 'hintH' not in site, 'the zone editor shortens the grid again; it must match the reader'
    # Paint and hit-test must come from that one grid: the hit-test lives in
    # zoneAtPoint() and only ever withholds the drawn hint row.
    zone_at_point = re.search(r'int TapZoneSettingsActivity::zoneAtPoint\(.*?\n\}', activity, re.S)
    assert zone_at_point, 'zoneAtPoint() is gone; the editor hit-tests somewhere else'
    body = zone_at_point.group()
    assert 'TapZoneGrid(screenW, screenH).zoneAt' in body, 'zoneAtPoint() no longer uses the reader grid'
    assert 'hintH' in body, 'zoneAtPoint() lost the hint-row exclusion'

    code = r'''
#include <cstdint>
#include <cstdio>

struct Rect { int16_t x, y, width, height; };
''' + grid + r'''
// Explicit returns rather than assert(): the check binary has to report the
// failing geometry, not abort without a message.
static int failures = 0;

static void expect(const bool ok, const char* what, const int a, const int b) {
  if (ok) return;
  printf("FAIL %s: %d != %d\n", what, a, b);
  ++failures;
}

int main() {
  // Every panel size with a touch controller in BoardConfig, in both
  // orientations: X4 Pro / X4 (480x800), X3 (528x792), the 800x480 landscape
  // panels, the 960x540 and 1216x684 boards, and the small 224x398 screen.
  // Geometry is derived from the size alone, so covering each size covers each
  // board; a fix validated only at 480x800 is not evidence for the others.
  const int panels[][2] = {{480, 800}, {528, 792}, {800, 480}, {960, 540}, {1216, 684}, {224, 398}};
  for (const auto& panel : panels) {
    const int w = panel[0], h = panel[1];
    // The editor and the reader both pass the raw screen size and nothing
    // else; comparing the two grids is comparing the two call sites.
    const TapZoneGrid editor(w, h);
    const TapZoneGrid reader(w, h);
    int mismatches = 0;
    int firstX = -1, firstY = -1, firstEditor = -1, firstReader = -1;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        const int a = editor.zoneAt(x, y);
        const int b = reader.zoneAt(x, y);
        if (a != b) {
          if (mismatches == 0) { firstX = x; firstY = y; firstEditor = a; firstReader = b; }
          ++mismatches;
        }
      }
    }
    if (mismatches != 0) {
      printf("%dx%d: %d of %d pixels disagree, e.g. (%d,%d) editor zone %d vs reader zone %d\n",
             w, h, mismatches, w * h, firstX, firstY, firstEditor, firstReader);
      ++failures;
    }
    // Every painted rectangle must resolve to its own index. This is what
    // catches a grid that runs off the panel at a size the designer did not
    // look at: the bottom row or the bottom mini strip would fall outside.
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const Rect r = editor.cell(row, col);
        expect(r.x >= 0 && r.y >= 0 && r.x + r.width <= w && r.y + r.height <= h, "main cell inside panel",
               r.y + r.height, h);
        expect(editor.zoneAt(r.x + r.width / 2, r.y + r.height / 2) == row * 3 + col, "main cell centre",
               row * 3 + col, editor.zoneAt(r.x + r.width / 2, r.y + r.height / 2));
      }
    }
    for (int i = 0; i < TapZoneGrid::kMiniCount; ++i) {
      const Rect r = editor.miniCell(i);
      expect(r.x >= 0 && r.y >= 0 && r.x + r.width <= w && r.y + r.height <= h, "mini zone inside panel",
             r.y + r.height, h);
      expect(editor.zoneAt(r.x + r.width / 2, r.y + r.height / 2) == 9 + i, "mini zone centre", 9 + i,
             editor.zoneAt(r.x + r.width / 2, r.y + r.height / 2));
    }
    // No main cell may overlap another: the six mini zones win over the main
    // grid, so an overlapping main grid would make some cells unreachable.
    for (int a = 0; a < 9; ++a) {
      for (int b = a + 1; b < 9; ++b) {
        const Rect ra = editor.cell(a / 3, a % 3);
        const Rect rb = editor.cell(b / 3, b % 3);
        const bool apart = ra.x + ra.width <= rb.x || rb.x + rb.width <= ra.x ||
                           ra.y + ra.height <= rb.y || rb.y + rb.height <= ra.y;
        expect(apart, "main cells do not overlap", a, b);
      }
    }
  }
  // Hint-row exclusion: only while the theme really draws the row. The check
  // mirrors TapZoneSettingsActivity::zoneAtPoint() for both cases, on a touch
  // board (rows never drawn) and on a button board (rows drawn).
  {
    const int w = 480, h = 800, hintH = 40;
    const TapZoneGrid grid(w, h);
    int hidden = 0, reachable = 0, wrong = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        // hintsDrawn: the theme-level gate the editor queries instead of the
        // raw metric, so a touch board with a non-zero metric stays fully live.
        for (int hintsDrawn = 0; hintsDrawn <= 1; ++hintsDrawn) {
          const int hint = hintsDrawn ? hintH : 0;
          const bool editorHas = !(hint > 0 && y >= h - hint);
          const int editorZone = editorHas ? grid.zoneAt(x, y) : -1;
          const int readerZone = grid.zoneAt(x, y);
          if (!editorHas) {
            if (readerZone >= 0) ++hidden;
            if (readerZone >= 0 && y < h - hintH) ++wrong;
          } else {
            ++reachable;
            if (editorZone != readerZone) ++wrong;
          }
        }
      }
    }
    expect(wrong == 0, "only the drawn hint row is withheld", 0, wrong);
    expect(hidden > 0, "the drawn hint row withholds some zones", 1, hidden);
    expect(reachable > 0, "everything else stays selectable", 1, reachable);
    // The bottom row's cells must not survive under the drawn hint row: with
    // 40px of hints, y >= 760 is the bar, and cell(2,*) ends at 792.
    expect(grid.zoneAt(80, 760) >= 0, "a bottom-row pixel still exists above the bar", 1, grid.zoneAt(80, 760));
    expect(760 >= h - hintH, "the bar starts at the last 40 rows", h - hintH, 760);
  }
  // The reported point, pinned. On the full-screen grid main cells are
  // 152x258 starting at y=6, so rows cover 6..264, 270..528 and 534..792:
  // (400,515) is inside cell(1,2) = zone 5. The editor used to build its grid
  // from screenH-40 (cells 245 tall) and painted this point inside cell(2,2),
  // so it configured zone 8 while a reader tap there turned zone 5's page.
  const TapZoneGrid reported(480, 800);
  const Rect cell5 = reported.cell(1, 2);
  expect(reported.zoneAt(400, 515) == 5, "reported point", 5, reported.zoneAt(400, 515));
  expect(515 >= cell5.y && 515 < cell5.y + cell5.height, "reported point inside cell 5", cell5.y, 515);
  // And the shortened grid must not reproduce the old answer, which is what
  // the editor would compute if it ever subtracted the hint row again.
  const TapZoneGrid shortened(480, 800 - 40);
  expect(shortened.zoneAt(400, 515) == 8, "shortened grid still maps the point to zone 8", 8,
         shortened.zoneAt(400, 515));
  if (failures == 0) printf("editor and reader grids agree on every pixel\n");
  return failures == 0 ? 0 : 1;
}
'''
    run(code, directory)
    print('Zone editor and reader share one grid geometry')


def main():
    with tempfile.TemporaryDirectory(prefix='tap-zones-') as temp:
        geometry(Path(temp))


if __name__ == '__main__':
    main()
