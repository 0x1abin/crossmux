# Inx tab layout validation

Validated against main `03a9c7ce4daa28fbac81c62b4d9b82d5a792b60e`, with its pinned
FreeInk SDK `34d36ecc37b192f0082cb889a8bb53c887b8d096` and simulator
`dacbbbcdc133a052f9122347429fdcb8cddf80af`.

The touch-only bottom-tab layout reserves a 28 px status bar inside the board's
viewable margins, then a 6 px gap, content, another 6 px gap, and navigation.
All five tabs share this geometry. The clock follows the configured format and
time zone; invalid time displays `--:--`. It updates on page renders, without
adding periodic display refreshes. Battery percentage follows its existing setting.

Bottom navigation is 56 px high and has no full-width separator. Its centered
38 × 5 px selected marker stays on the top edge. The 38 px icons sit 6 px above
the navigation area's bottom edge, leaving a 7 px gap below the selected marker.
The inset is visual padding inside navigation, additional to the board's safe
margins. Touch bottom-tab pages gain 6 px of content compared with the tested
32/58 px bars, or 26 px compared with the earlier 44/66 px bars.
Physical-button hints and the tabs' 6 px horizontal gaps remain.
Top-tab rendering and the reader/subpages retain their existing geometry.

## Layout ownership

- `Activity::mainTabLayout()` resolves device margins once and returns the
  complete status, content and navigation rectangles. Drawing and input consume
  those rectangles; there is no retained layout cache or extra allocation.
- `Activity::pageContentRect()` owns the main-tab / regular-header choice for
  all five pages. Individual pages only reserve their own internal spacing.
- App-grid drawing and touch lookup share the same content rectangle and cell
  bounds. Geometry uses the existing `Rect` type from a lightweight header,
  without importing theme implementations or duplicating rectangle types.
- Tab preference defaults are initialized directly from the board's touch
  capability. Existing saved values and the settings schema are unchanged;
  no separate default-value function or host-test stub is needed.

## Automated checks

```sh
cmake -S test -B build/test
cmake --build build/test --target InxNavigationTest TimeUtilsTest -j 4
ctest --test-dir build/test -R 'InxNavigation|TimeUtils|ControlCenterGesture|InxStyleCompatibility' --output-on-failure
cmake --build build/test -j 4
ctest --test-dir build/test --output-on-failure -j 4
python3 -m unittest discover -v -s scripts/tests
./bin/clang-format-fix --check
pio run -e simulator -e simulator_eego_a4 -e simulator_murphy_m4
```

The 30 focused checks cover tab order, drawing/hit bounds and gaps, status-bar
eligibility, content reservations, app-grid hit bounds, and valid/invalid
12/24-hour time formatting. Layout tests include nonzero horizontal/vertical
safe-area origins and theme top padding in both tab positions. The control-center
dispatch harness uses the production layout type and exercises all five pages
at each status-rectangle edge and outside it, including the content gap,
zero-height status bars (top tabs), and non-touch input.

The compact-layout checks additionally cover the 28/56 px bars, 6 px content
gain, and control-center rejection of taps inside the former taller status area.
All 30 focused checks pass, as do two existing two-bit renderer checks, four
Metalio firmware/display checks, one SDK charger check, five reading-UI checks,
repository formatting and whitespace checks.
The complete host suite passes 579/579 tests. Python discovery runs 72 tests:
71 pass and one font-regeneration check is skipped by its default policy.

The optimized rectangle fill now intersects the existing logical clip rectangle
before rotation, keeping partial trailing rows inside their list body. The host
regression compares the production fill against a per-pixel reference across all
four orientations, solid/dither fills, off-screen and empty clips, and full/strip
framebuffers. It fails without the correction and passes with it:

```sh
python3 -m unittest discover -v -s scripts/tests -p 'test_gfx_fill_clip.py'
```

## Native simulator checks

All twelve native scenarios pass using isolated simulated SD cards:

| Device | Scenarios |
| --- | --- |
| A4 | English empty state, Chinese 25-book list, English landscape, top tabs, hidden battery percentage |
| Murphy M4 | Chinese empty state, English 25-book list, Chinese landscape, top tabs, Classic theme |
| X4 (no touch) | Default top tabs and explicitly selected bottom tabs, with button navigation |

Each Inx run visits Recent, Library, Apps, Settings and Statistics.
Saved Top and Bottom preferences are retained. On the 28/56 px layout, additional
480 × 800 native runs open and close the control center on all five pages,
reject taps in the former taller status area and between adjacent tabs, and open
the first and last books of a 25-book library using touch navigation.

The visual checks verify the absence of a full-width bottom separator, the
38 × 5 px selected marker, 7 px marker-to-icon gap, 6 px bottom padding,
time/battery visibility, preserved safe margins and X4 button hints. Murphy M4
portrait uses 480 × 800; A4 and landscape runs provide additional regression
coverage. All 56 page pixel checks pass, including the A4 Chinese long-list
library page with a partial trailing row. Its background no longer overwrites
the path band or selected Tab marker. Top-tab and Classic checks retain the
previous rendering. Verification artifacts stay in local ignored directories.

Use `CROSSPOINT_SIM_SD` to select an isolated SD directory. Its
`.crosspoint/settings.json` can start with
`{"uiTheme":5,"language":"EN","onboardingVersion":1,"clockUtcOffsetQ":80}`.
Omitting `inxTabPosition` tests the board default; `0` selects top, `1` bottom.
Use `language: "ZH_CN"` and `clockFormat: 1` for Chinese and a 12-hour clock.

Example A4 bottom-tab navigation:

```sh
CROSSPOINT_SIM_SD=/tmp/inx-sd \
CROSSPOINT_SIM_INPUT_SCRIPT='2100:TAP:0.3,0.93;3200:TAP:0.5,0.93;4300:TAP:0.7,0.93;5400:TAP:0.9,0.93;6500:TAP:0.3,0.04;7800:QUIT' \
xvfb-run -a .pio/build/simulator_eego_a4/program
```

Main menus currently run in portrait. The landscape stress checks temporarily
set the live renderer orientation using GDB at `InxRecentActivity::onEnter()`;
they do not add a menu-rotation setting. Use normalized scripted coordinates
because the simulator parses its input schedule before this breakpoint.

```gdb
break InxRecentActivity::onEnter()
commands
silent
call (void) 'GfxRenderer::setOrientation(GfxRenderer::Orientation)'(&renderer, 1)
disable 1
continue
end
run
```

Physical touch-controller behavior, EPD ghosting, refresh timing, and power
consumption still require device validation; the simulator does not model them.

## Hardware build results

The compact Metalio test build uses `pio run -e metalio_eink4` with the existing
isolated PlatformIO core/package directory and serial logging enabled. Its
application image is intended for the existing CrossMux Wi-Fi update flow.
Build sizes, image identity, checksums and test logs accompany the local artifact.
All three simulator builds and the Metalio build pass. Metalio reports 99,732
bytes static RAM and 5,932,315 bytes Flash; the application file is 5,932,816
bytes and fits the 6,553,600-byte OTA slot. These are whole-image sizes.
Chip ID, board tag, source/binary partitions and image integrity checks pass.

The Metalio candidate was written to the existing test device's active
application slot, independently verified, and USB-reset into the Inx Recent
page with its saved settings. The application SHA-256 is
`0fcce94784607a994c6635b86d964408063cb5d91a1619f13f94da5750a9bdbb`.
The commit-history cleanup does not change the tested source. These checks
establish flashing and startup, not physical layout/touch/power acceptance.

The default C3 build compiles but fails to link with missing
`ble_base_funcs_reset`, `ble_42_adv_funcs_reset` and related BLE controller symbols
referenced by ESP-IDF's `bt.c`. The same failure occurs in the existing package
cache, a freshly provisioned isolated core, and an unmodified checkout of main
`03a9c7ce`. Therefore the C3 build is **not passing in this environment**; the
baseline comparison establishes that this failure also occurs without the Inx
changes. No BLE/toolchain workaround is included in this UI change.
This baseline comparison was recorded previously; the C3 build was not rerun
for the compact-layout update.
