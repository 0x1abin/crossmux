# Inx tab layout validation

## Current upstream rehearsal

The fixed local rehearsal uses Reader `38280863`, SDK `98b4e427` plus all six
ReadPico fixes through `e3550ec`, and Simulator `20e73803`, on CrossMux `593c8dbc`.
The reviewed source-tree fingerprints are recorded with the rehearsal artifacts;
these are source exports, not replacement production commits. Existing typography,
fallbacks, explicit high-density opt-in and calibrated safe insets are retained.
Reader menus and the down-swipe control center use the upstream implementation in
all themes, including INX and ReadPico; the Text panel replaces Focus Reading with
First Line Indent. Ordinary INX settings keep their own controls and shared
swipe/drawing geometry. Historical captures and the preceding deployment record
below remain unchanged. This rehearsal does not flash hardware.

## Historical current behavior

The 2026-10-02 integration includes current main and PR #357, with FreeInk SDK
`6c2f82245ff6c5e3ac5df582c70ca674a84e1f1e` and simulator
`33e585ff6452ea03f6a164f51e379f065e8c2e54`. ReadPico's portrait safe insets are
`{5,5,8,5}` in both the SDK and simulator.

The Tab-position setting appears only for Inx, in the device UI and
English/Chinese web settings. Touch devices default to Bottom; button-only
devices default to Top. Existing saved choices and the settings schema are
preserved.

Touch Inx bottom-tab pages share a 28 px upper status area, a 6 px gap,
content, another 6 px gap, and 56 px navigation. Recent, Library, Apps, Settings
and Statistics display local time on the left and battery on the right.
Time follows the configured format and time zone; invalid time shows `--:--`.
It updates only on page renders, without periodic refreshes or wake-ups.
Tapping the upper status area opens the existing control center.

Bottom navigation has no full-width separator. The centered 38 × 5 px selected
marker stays on its upper edge; 38 px icons sit 6 px above its bottom edge,
leaving 7 px below the marker. Oriented hardware margins, physical-button hints
and the tabs' 6 px horizontal gaps remain reserved.

With top tabs, touch Inx Recent displays only its bottom-right battery inside
the existing 40 px footer. It neither formats nor draws a footer clock and adds
no footer touch action. Both status placements use a 12 px logical-screen-edge
inset, with hardware margins as minimum limits rather than additive padding.
Clock and battery percentage share the numeric font and baseline; positioning
compensates for the battery icon's internal 6 px offset and inclusive edge
pixels. Drawing is clipped to the supplied rectangle; percentage visibility
uses the existing setting.

Top-tab navigation, non-touch home battery, other themes, reader/subpages,
content reservations and input rectangles keep their established behavior.

### Ownership and review

- `Activity::mainTabLayout()` returns status, content and navigation rectangles;
  drawing and hit testing share them. `pageContentRect()` owns the main-tab /
  regular-header choice; pages reserve only their own internal spacing.
- App-grid drawing and touch lookup share cell bounds and the lightweight
  `Rect` type. No layout cache or extra allocation is introduced.
- `drawMainTabStatusBar()` owns both status placements. Only bottom tabs use
  the existing time formatter and 9-byte stack buffer; the top-tab Recent
  footer reuses its battery alignment without a second clock implementation.
- The existing separate renderer correction intersects logical clipping before
  rotation, preventing partial list-row fills from covering neighboring UI.
- Review found no need for another production abstraction. The latest update
  publishes the final corner alignment directly, without intermediate
  clock-addition/removal commits, and consolidates repeated validation prose.
  No public interface, dependency or configuration is added by this follow-up.

## Verification methods

```sh
cmake -S test -B build/test
cmake --build build/test -j 4
ctest --test-dir build/test --output-on-failure -j 4
ctest --test-dir build/test -R 'InxNavigation|TimeUtils|ControlCenterGesture|InxStyleCompatibility' --output-on-failure
python3 -m unittest discover -v -s scripts/tests
./bin/clang-format-fix --check
git diff --check
pio run -e simulator -e simulator_eego_a4 -e simulator_murphy_m4
pio run -e metalio_eink4
```

Host checks cover tab order, shared drawing/hit bounds, inert gaps, status
eligibility, safe-area origins, content reservations, app-grid boundaries,
control-center edge taps, time formatting, Metalio firmware/display behavior
and renderer clipping. The production status/footer seam additionally checks
hardware limits, percentage visibility, invalid time, both clock formats and
that the top-tab footer does not call time formatting.

Native runs use isolated `CROSSPOINT_SIM_SD` directories and the simulator's
scripted input interface. For example, its `.crosspoint/settings.json` can start
with `{"uiTheme":5,"language":"EN","onboardingVersion":1,"clockUtcOffsetQ":80}`.
Omitting `inxTabPosition` tests the board default; `0` selects Top and `1`
Bottom. Use `language: "ZH_CN"` and `clockFormat: 1` for Chinese and 12-hour time.

Main menus normally run in portrait. Landscape stress checks temporarily
change the live renderer orientation at `InxRecentActivity::onEnter()` using
GDB; they add no menu-rotation setting. Normalized input coordinates account
for the simulator parsing the schedule before this breakpoint. A separate
GDB override fixes time or injects invalid time and larger status bounds.

## Latest local results

The final-layout verification on 2026-09-30 retains the source behavior of the
previously verified no-footer-clock candidate. Logs and local artifacts remain
in ignored directories; none are part of the PR.

- Complete host suite: 579/579 pass. Python discovery: 72 pass and one
  font-regeneration check skipped by default policy. Formatting and whitespace
  checks pass.
- All 38 native scenarios pass: A4/M4 empty and 25-book lists, English/Chinese,
  top/bottom navigation, Classic, button-only X4, percentage visibility, invalid
  time, larger hardware bounds, and all five populated Recent layouts at
  480 × 800 and 800 × 480. Pixel comparisons confirm all 196 rendered frames
  match the previous final-layout baseline exactly.
- Additional 480 × 800 interaction checks pass: control-center open/close on
  all five pages, inert status/tab gaps, passive footer, and first/last library
  items opened by touch. Saved Top/Bottom preferences are retained.
- `simulator`, `simulator_eego_a4`, `simulator_murphy_m4` and regular
  `metalio_eink4` builds pass. Metalio reports 99,732 bytes static RAM and
  5,932,515 bytes Flash; these are whole-image totals, not measured savings.
- The application is 5,933,008 bytes and fits the 6,553,600-byte OTA slot.
  ESP32-S3 identity, Metalio board tag, partition layout, checksum and image
  validation hash pass. SHA-256:
  `8db1ef0ede41f2e76c7c29e8fb6e6347f004a3ea7dd59b376e73be5c0c8b1821`.

This rebuilt application is stored in
`.cache/firmware/metalio_eink4-inx-pr-final/`, with its build and validation logs.
It is an application image for the existing CrossMux Wi-Fi update flow, not a
merged full-install image. It has not been flashed. The earlier no-footer-clock
candidate and device backups are preserved.

## Historical device records and limits

The following records identify older images; they are not flashing or physical
acceptance evidence for the current candidate.

- Compact-v2 application: 5,932,816 bytes, SHA-256
  `0fcce94784607a994c6635b86d964408063cb5d91a1619f13f94da5750a9bdbb`.
  The active application slot was flashed and independently verified; USB reset
  loaded saved settings and reached Inx Recent.
- Corner-alignment application with the now-removed footer clock:
  5,933,008 bytes, SHA-256
  `c0968a79e03a4a857964c8bb0cdb0d2287a32a7f648db24d034fa9c7b1ef2b7e`.
  After explicit authorization on 2026-09-29, the same Metalio device's complete
  active app1 slot was backed up and verified before writing at `0x650000`.
  Write and independent application verification matched; USB reset identified
  Metalio, loaded saved settings and entered Inx Recent. No bootloader, partition
  table, OTA metadata or configuration partition was manually rewritten.
- Initial no-footer-clock candidate: 5,933,008 bytes, SHA-256
  `4be4b6aad317861ce58cd629f6f3910987c1370ea56cd7675354a156c7a873cf`.
  Its 30 related host checks, 72 Python checks plus one skip, three simulator
  builds and regular Metalio build passed. Across 196 native frames, 25 lost
  only the footer clock and all other pixels remained identical. It was not
  flashed.

A previously recorded local C3 link failure involved `ble_base_funcs_reset`,
`ble_42_adv_funcs_reset` and related BLE symbols. It was reproduced on
unmodified main `03a9c7ce`; C3 was not rerun for this follow-up. This is a local
baseline limitation, not a claim about current remote CI. No BLE/toolchain
workaround is included.

Simulator checks and flashing/startup records do not establish physical bezel
occlusion, touch feel, EPD ghosting, refresh timing or power consumption. Those
remain device acceptance items.
