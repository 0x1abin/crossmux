# High-density UI profile

Status: existing NotoSans typography, splash and top battery layout were confirmed
in the native simulator on 2026-10-02. The latest requested reader footer uses
5px visible bottom clearance and 14px side insets; native captures verify the
5px downward and 18px outward movements from the preceding confirmed layout.
The profile is enabled in `readpico_hardware`, inherited by `readpico` and
`readpico_nightly`, and in `simulator_readpico`. The existing-font/SVG build has been flashed without backup to the connected
ReadPico before the PR rebase and passed a 40-second startup/heap smoke test. Visual refresh and
touch acceptance on the physical panel remain pending.

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

## Historical integration baseline

- CrossMux [PR #357](https://github.com/0x1abin/crossmux/pull/357) merged as `1cfb2fb209e2b9c8ca6e50d08f6764dffde40ccf`.
- CrossMux [PR #291](https://github.com/0x1abin/crossmux/pull/291) merged as `a08147cff0a9a197f83e49b033d960a5ed11a334`.
- Simulator [PR #8](https://github.com/0x1abin/crosspoint-simulator/pull/8) merged and pinned at
  `33e585ff6452ea03f6a164f51e379f065e8c2e54`; native ReadPico insets match the SDK:
  top/right/bottom/left `5/5/8/5`.
- FreeInk SDK stays at `6c2f82245ff6c5e3ac5df582c70ca674a84e1f1e`.

## Design and code ownership

The UI preset is calibrated for roughly 300 PPI; this is a density class, not
an exact panel PPI or an automatic numeric threshold. Device identity, native
resolution, bezel data and hardware capabilities remain in the board profile.
Targets opt in explicitly with `CROSSMUX_UI_PROFILE_HIGH_DPI`, owned by
[`UiHighDpiProfile.h`](../../src/components/UiHighDpiProfile.h). Only ReadPico
hardware/Nightly and its simulator currently select it. Other similar-density
panels may reuse the fonts, icons and control sizes after viewport validation;
a smaller panel still has less available space. Layout uses actual oriented
dimensions and safe areas. Memory and PSRAM policies remain independent of PPI.
The current validation panel is ReadPico's 1216 × 684 native display,
684 × 1216 in portrait orientation.

The existing theme, scaling, list and toolbar paths own rendering and input.
`Activity.cpp:55` computes the shared tab/content bands and
`MainTab.h:23` owns tab hit bounds. `UITheme.cpp:38` binds the fixed UI fonts;
`SdCardFontSystem.cpp:301` keeps reader-family changes from replacing them.
`InxItemLayout.h:65` supplies the grid rectangles to both drawing and hit testing.
This avoids a parallel rendering framework or a second framebuffer.

| Element | Dimensions in native logical pixels |
| --- | --- |
| Content side inset / adjacent control gap | 32 / 12 |
| Main navigation / status band | 96 / 48 |
| Subpage header | 112 |
| Single-line / subtitle list row | 104 / 128 minimum |
| Control-center tile / reader chapter step | 96 |
| Main navigation / list and reader toolbar icons | 56 / 48 |
| Status / reading footer battery canvas | 32 × 20, generated from Lucide SVG |
| Reader text/status lane reservation | 48, plus the configured progress bar |
| Reader footer text / estimate | 12 / 14pt |
| Reader bookmark / Bluetooth markers | 24 × 24 |
| Reader footer side inset / visible Chinese bottom gap | 14 / 5 |

Fonts use the repository converter's 150-DPI point convention, not the physical
panel DPI. Small text, battery numbers and reading footer use existing NotoSans
12; body text uses NotoSans 14 Regular/Bold; titles and control panels use
NotoSans 16 Regular/Bold. Chinese UI uses existing CJK 12/14/16 at the matching
size. The 14/16pt resources cover interface text, not arbitrary book names.
The renderer checks coverage of the complete text run and follows each subset's
fallback to the broader common CJK 12 face: “梦海” must render both characters,
even though the 14pt subset covers 海 but lacks 梦. Ubuntu 12 Regular/Bold is
last in the UI chain for Hebrew and shaped Arabic/Persian/Urdu text.
The high-density measure/draw paths select fonts after the existing bidi/Arabic
conversion, reusing its visual string instead of allocating a second buffer. Text runs
mixing scripts which no single existing face covers remain limited by the
existing whole-run renderer; unsupported characters keep its missing-glyph
behavior. No new public font interface is introduced.

A new profile retains the saved 14pt default and 25px margins. Complete Chinese
reading at 14pt requires an installed SD font. Without SD, the selectable built-in
size is 12pt and the reader uses existing common CJK 12. Resolving this fallback
and returning from SD fonts do not overwrite a saved point size on this profile.
Installed reader families retain their available sizes. Fixed UI bindings survive
bitmap/vector reader font changes. Matching CJK UI subsets remain first, then
the selected SD family supplies missing book/folder names at 12/14/16pt, then
common CJK 12 and Ubuntu 12. SD footer faces still load at 12/14pt.

The 48px reader status lane reserves 1px below the NotoSans 12 line. Centering
the existing CJK 12 fallback within that line leaves the requested 5px visible
gap below the Chinese footer ink; the physical 8px bottom bezel is excluded.
Latin descenders and SD faces retain their own metrics. The estimate uses only
the 14pt `~` glyph, which fits inside the footer line. Oriented bezel insets and
configured progress-bar space remain authoritative.

Reader pagination shares the footer's font-dependent top-padding calculation.
When the text lane or automatic-page-turn title is present, it reclaims that
padding above a 6px content-to-footer gap, while retaining the configured screen
margin and oriented bezel insets. Footer coordinates do not move. A 34px footer
line reclaims 7px from the 48px reservation; whether this fits another complete
body line depends on the current font and pagination remainder. Hidden and
progress-only footers keep their existing reservation unless automatic page
turning needs a title. EPUB, TXT and Markdown share this layout. A changed
viewport invalidates complete and partial section caches through the existing
render spec and restores reading position by visible-text offset; no binary
format or saved reading setting changes. No allocation is added.

The host regressions execute the production padding and reader-margin paths in
four orientations, with text/progress visibility, automatic turning, varying
footer font heights and both profile modes. Section tests cover viewport-driven
complete/partial cache invalidation and lossless text-offset restoration. Native
ReadPico captures with built-in 12pt text and 31px margins show 32 to 33 complete
lines after the viewport grows from 1119 to 1126px. An SD NotoSans 16pt case with
25px margins grows from 1125 to 1132px and retains 25 lines. The footer battery
region is pixel-identical in both comparisons; physical-panel acceptance remains
pending.

The new built-in reader ID `0x4738000C` replaces the earlier 4.7-inch custom-font
IDs. `Section.cpp` checks that identity and rebuilds old pagination using the
existing cache format; book progress is retained. The `screen47` font directory,
its generator and converter-only support have been removed. No fonts are
regenerated for this revision.

## Boot and default sleep splash

`BaseTheme::drawSplash()` is shared by `BootActivity::renderSplash()` and
`SleepActivity::renderDefaultSleepScreen()`. On the high-density profile it centers
the complete logo/title/status block in the oriented safe area. The existing
120px static logo is drawn at its native size; the title uses the resident
16pt bold face and the boot/sleep status uses the resident 14pt face. Logo-to-title
space is 24px; title-to-status space is 12px after the measured title line height.
The 12pt firmware version sits 32px above the safe bottom edge after reserving its
full line height. Other profiles retain the former coordinates and font IDs.

This replaces the former 25px title-to-status offset, which overlaps with the
larger UI font, and the 30px bottom offset that clips the version. No new bitmap,
framebuffer, font or allocation is introduced. OTA validation/preload screens
remain in their existing rendering path. `test_reading_ui_regressions.py`
executes the production splash drawing with all four safe-area orientations,
larger font heights, both boot/sleep text and optional version; it also checks
legacy coordinates.

## Resource budget and regeneration

The profile reuses the existing font decompressor and PSRAM-preferring
allocator (`FontDecompressor.cpp`). Existing CJK 12/14/16 bitmaps are uncompressed
and occupy 525,663 / 190,768 / 243,953 bytes before link-time resource removal.
Existing NotoSans Regular 12/14/16 maximum decompressed groups are
20,261 / 27,313 / 34,821 bytes, exceeding the former custom 16KiB group limit.
The existing allocator supplies their transient buffers; there is no new buffer
implementation or framebuffer. Firmware capacity and live heap must be measured
before claiming savings.

Font objects and bitmap tables are static. Four additional face registrations
(common reader revision, CJK 14/16 and Ubuntu script fallback) reuse existing
renderer map nodes. Three additional fallback-chain nodes keep these faces
reachable without a new font registry or duplicated bitmap data. Selection uses
an eight-ID stack array with cycle rejection, no per-string heap allocation.
C3 builds do not enable the UI profile macro or link the additional resources.
The existing SD manager reuses sizes shared with the reader/footer, adding at
most the missing 16pt UI face. Vector UI fallback reuses its existing PSRAM/
internal-heap guard and 16KiB glyph cache per lazy face. UI/footer share the
12/14pt faces, so only three distinct UI sizes are owned; the five logical
slots reserve their existing owner vectors before insertion. No font source
bytes are duplicated, and unload removes SD candidates without deleting
built-in CJK/common/Ubuntu mappings.

Tab artwork is generated directly at 56 × 56 from pinned Lucide `house`,
`folders`, `layout-grid`, `settings-2`, and `chart-line` SVGs. The main
Tab order is unchanged (recent/library/apps/settings/statistics). The former
38 → 56 nearest-neighbor rendering is removed. Legacy targets still use
`inx_tabs.h` at 38px.

The battery outline comes from `battery.svg`; the charging marker uses only the
lightning path from `battery-charging.svg`, fitted into the fill cavity before
rasterization. Both are rasterized at 32 × 32, then rows `[6,26)` are retained
without resampling, producing 32 × 20. White bits are transparent; only ink bits
are drawn (black for the outline, white for the charging marker). Filling is
dynamic and clamped to 0–100%; charging keeps enough fill under the lightning
marker. All data is static, with no new runtime allocations. The bitmap payload
is 2,680 bytes (five 392-byte Tabs, two 80-byte battery planes,
32/48px settings variants totaling 416 bytes, and two 72-byte footer markers). All settings entrypoints use
`settings-2`: the Tab, FUI list rows and legacy bitmap-based menu buttons.

The shared theme profile sets battery metrics to 32 × 20 and reserves a 48px
reader text lane so the 12pt text and 24px auxiliary icons clear the progress bar.
Footer percentages, clock, chapter title and counters reuse the resident 12pt
face; the estimate marker reuses 14pt. SD reading families load footer faces at
12/14pt, while the other UI faces remain fixed. Footer side insets are 14px,
auxiliary-icon gaps are 12px, and a title with no remaining space is omitted.
The former 8pt footer is no longer linked by the high-density font setup. INX/Lyra battery
rendering uses the same generated assets in main status bars, subpage headers,
and reading footers. FreeInkUI headers retain their SDK component renderer,
using the updated dimensions. Viewport height is part of the existing reader
render spec/cache identity, so the lane change rebuilds affected pagination
without a binary format or settings migration.

Regenerate chrome using the existing SDK converter (`rsvg-convert` and Pillow):

```bash
python3 scripts/build_ui_chrome_icons.py
clang-format-21 -i src/components/icons/uiChromeIcons.h
python3 scripts/tests/test_ui_chrome_icons.py
```

The manifest and generator record source names, stroke width, threshold, crop,
and charging-marker transform. SVG originals, their upstream revision, SHA-256
checksums, licenses and commands for other output sizes are maintained in
[`sources/lucide/README.md`](../../src/components/icons/sources/lucide/README.md). No SDK or simulator pin changes are required.

List and reader toolbar icons come from the repository-maintained
Lucide SVG sources in `src/components/icons/sources/lucide`. Generate with
`freeink-sdk/libs/assets/Icons/tools/gen_icons.py`, using `--sizes 48 --out <output>`:

| Manifest | Size | Output |
| --- | --- | --- |
| `src/components/icons/listIcons.manifest` | 48 | `listIcons48.h` |
| `src/components/icons/readerToolbarIcons.manifest` | 48 | `readerToolbarIcons48.h` |

## Native review and acceptance

The review artifacts live in `build/screen47-preview/`. Screenshots come from
the actual SDL simulator's unscaled pixels, using an isolated sample SD card
without installed fonts; they are not HTML layout mockups. `review.html` links
the full-size images, and `launch.sh` runs the same preview binary and SD card
on a graphical desktop.

```bash
pio run -e simulator_readpico -t run_simulator
python3 scripts/tests/test_reading_ui_regressions.py
python3 scripts/tests/test_ui_font_fallback.py
```

Review home, book grid/list, reading text, reader toolbar/text panel, settings,
option dialog and control center. The focused geometry check covers the four
oriented safe areas, both tab positions, tab/grid gap rejection and matching
draw/hit rectangles. Native smoke screenshots additionally cover the five
home layouts and four reader orientations. Check long titles and saved 12pt
settings alongside the fresh 14pt defaults.

The confirmed profile is enabled through the shared hardware build flags;
both ReadPico environments inherit it. Verify on the physical panel:
glyph weight, AA and refresh quality, bezel clearance, touch gaps,
font switching, page turns, and available/largest heap blocks through Serial.
Simulator captures and successful builds do not establish physical acceptance.

## PR candidate and latest reader footer (2026-10-02)

The feature commit was rebased onto CrossMux `main` at `237ca0c0`, retaining
the Chinese settings/font completion, INX switch repair and unified date/time
changes. No SDK or simulator revision changed. Before the final footer adjustment,
full `bin/ci-check` passed all eight hardware targets, formatting, default-target
cppcheck, 821 host tests and SDK display/resource/PMU checks. An initial X4 Pro
attempt used a modified SDK cache: moving the isolated core under a `.platformio`
path allowed the existing builder to restore the original framework between
custom and prebuilt targets, and the complete rerun passed.

The latest footer changes only high-density reader metrics: the internal bottom
padding is 1px, yielding 5px below the existing centered Chinese ink, and the
side inset is 14px rather than 32px (22px was the intermediate preview).
General content padding remains 32px;
status-lane reservation, fonts, cache IDs and continuous progress-bar geometry
are unchanged. Native portrait captures measure the battery group at x=21
instead of 39, the right progress group at x=576 instead of 558, and the Chinese
title's last ink row at 1202 instead of 1197. The last viewable row is 1207;
the eight physical bezel rows are excluded from this measurement.

After this adjustment, the six SVG/battery/footer checks, 14 layout/cache checks,
formatting, all 821 host tests and the normal ReadPico build passed again.
Ten repeated reading captures cover four orientations, SD reading, battery
0/50/100/charging and hidden percentage. The reading body remains pixel-identical,
and measured group shifts from the preceding confirmed layout are exactly
(-18,+5), (0,+5) and (+18,+5). Against the intermediate 22px-side preview,
the left/right groups move exactly -8/+8px with unchanged vertical positions.
The normal firmware is 6,133,808 bytes, leaving 419,792 bytes in the OTA slot;
linker Flash/static RAM are 6,128,603 / 110,272 bytes. These measurements do not
claim savings over `main` or predict runtime heap.

Native preview and comparison artifacts are under `build/high-dpi-footer-narrow/`:
`reading-portrait-5px-14px.png`, `reading-landscape-5px-14px.png`,
`footer-before-after-8px.png` and `footer-validation.json`. The intermediate
22px-side preview remains under `build/high-dpi-footer/`. Post-rebase captures before
the footer change are under `build/high-dpi-pr/`; 42 of 44 frames match the
preceding captures after excluding clocks, with settings-label changes confined
to two captured frames. Final Nightly packaging measurements are recorded in
the PR description and the local `build/high-dpi-footer-narrow/` validation artifacts.

Hardware evidence below belongs to the earlier local
`1.6.5-readpico-rc+cfa0c36` image, not this rebased 5px-footer candidate.
Its hash-verified application-only flash and 40-second serial smoke test remain
recorded; physical refresh, touch and long-running acceptance are pending.

## Historical high-density profile cleanup and maintained sources (2026-10-02)

The former screen-specific compile flag and C++ namespace have been replaced
by `CROSSMUX_UI_PROFILE_HIGH_DPI` / `UiHighDpiProfile`. This is an explicit UI
preset selection for similar-density panels, not a numeric PPI test. The only
current opt-in targets remain ReadPico hardware/Nightly and its simulator.
Repeated control-gap, content-padding and control-icon sizes reference the
existing profile constants. Fonts, fallback order, saved settings and the
built-in reader ID `0x4738000C` retain their behavior and values.

Chrome resources use purpose/size names; 48px list and toolbar resources live in
`listIcons48.h` and `readerToolbarIcons48.h`. Drawing only the ink pixels is
named `drawTransparentBitmap()`. All 33 generated bitmap arrays and the chrome
optical centers match the preceding resources exactly.

Thirty unmodified Lucide SVG originals, upstream licenses, revision and SHA-256
checksums are maintained directly in `src/components/icons/sources/lucide`.
Generators read these sources while reusing the pinned SDK converter. The
five Tab originals were also successfully converted at 40/64px into temporary
outputs, without changing firmware resources.

The 44 repeated native captures cover major pages, all selected Tabs,
0/50/100/charging battery states, hidden percentage, reader orientations,
boot/sleep, multilingual names and SD-font reading. After masking only the
clock digits, every frame is pixel-identical to the preceding build. The
reader footer still has 10px of visible bottom space. Production simulator
binaries exclude the temporary battery hooks and their HAL sources were
restored after the fixture build.

Existing host layout checks now additionally cover 600×1000 and rotated
1000×600 viewports with oriented insets, splash containment, Tab/grid gaps,
battery/status containment and reader-footer fields. This is geometry
validation; no additional physical device is declared supported. Six icon,
14 layout/cache and six font-fallback checks pass, including repeated SVG
regeneration, and the 32 Nightly packaging checks pass.

Comparison artifacts are under `build/high-dpi-refactor/`, including
`pixel-comparison.json`, `layout-equivalence.png` and
`major-pages-final-preview.png`. Historical artifacts retain their paths.

Full `bin/ci-check` passed: formatting, cppcheck, all eight hardware targets,
817 host tests and the SDK display/resource/PMU checks. Initial attempts failed
on sandbox cache permissions and the component manager's required network
fetch. Final builds used isolated PlatformIO/uv/component caches and network
access for the fixed dependency check. No SDK or simulator revision changed.

ReadPico linker Flash/static RAM are 6,108,215 / 110,808 bytes; Nightly reports
6,061,511 / 110,784 bytes. Static RAM matches the preceding build. Linker Flash
increased by 80 / 96 bytes respectively; this cleanup does not claim savings.
The validated Nightly package is 6,066,720 bytes, leaving 486,880 bytes in its
6,553,600-byte OTA slot. Both locale compatibility manifests match the embedded
version `1.6.5-readpico-rc+cfa0c36` and board/chip metadata. Firmware SHA-256:
`ff9c3eb5ba460954a9dd3a1dc14266540b9c56cfceaa015a4f703bfec207289e`.

The Nightly image was flashed without a firmware backup to the connected
ReadPico's current app0 at `0x10000`, after validating OTA metadata sequence/CRC.
Only that application was written; esptool verified its hash. A 40-second
capture detected ReadPico, the 1216×684 panel, SDMMC mount, RTC and IMU, with no
current panic, OOM or out-of-range drawing messages. Internal free heap ranged
from 37,599 to 44,583 bytes, largest blocks from 25,588 to 31,732 bytes and the
boot minimum reached 10,964 bytes. PSRAM free ranged from 5,019,540 to 6,086,588
bytes. Activity/cache state varied during this capture, so these numbers do not
establish a general memory or performance improvement. Physical refresh quality,
touch behavior and long-running stability still require device-side observation.
See `build/high-dpi-refactor/device-test/` and `validation.json` for records.


## Historical custom-font simulator validation (superseded)

- Full `./bin/ci-check`: formatting, cppcheck, all eight hardware environments,
  817 host tests and SDK display/resource/PMU checks passed. These hardware
  environments keep the profile disabled.
- All five native environments built successfully: X4, X3, eego A4, Murphy M4,
  ReadPico. The ReadPico preview was rebuilt after adding the independent
  built-in reader/cache IDs.
- Focused checks cover profile geometry, top-tab footer containment, legacy
  versus profile reader IDs, SD font precedence, fixed UI survival across
  bitmap/vector font changes and custom-resource checks (the custom resources are now removed).
- Temporary build-only `readpico47_preview` enables the screen flag to check the
  actual hardware image size: **6,378,103 / 6,553,600 bytes**, leaving
  **175,497 bytes**. Static RAM reported by the linker is 110,840 bytes;
  this excludes dynamic heap use and does not substitute for a device heap test.
- Native captures cover both tab positions, five home layouts, book grid/list,
  four reader orientations, menus, settings and control center with no SD fonts.
  Saved 12pt/10px preferences survived loading and reading. The Xvfb capture
  acceptance checks validate saved frames/dimensions; both the unchanged
  baseline and candidate return 1 on the scripted Xvfb quit in this environment.
  Clean window teardown is therefore not claimed by the screenshot check.

The Tab artwork was restored to the original INX bitmaps after review; native
ReadPico and the temporary hardware candidate rebuilt successfully, the 13
geometry regressions passed, and affected screenshots were recaptured. Battery
dimensions were subsequently restored to the original theme metrics; the shared
outline/fill/charging renderer remains unchanged. Both native and hardware
candidate builds plus the 13 geometry regressions passed again.

## Historical custom-font production validation (superseded)

- The shared `readpico_hardware` flags enable the screen specification in both
  `readpico` and `readpico_nightly`; the native simulator also enables it. The
  other seven hardware environments retain their existing screen profiles.
- Full `./bin/ci-check` passed after activation: formatting, cppcheck with no
  defects, eight hardware builds, 817 host tests and SDK display/resource/PMU
  checks.
- Focused checks passed: 13 layout regressions, four font lifecycle checks,
  six font resource checks (one optional regeneration check skipped), and 32
  Nightly release tests.
- The production ReadPico linker reports 6,377,975 Flash bytes and 110,840
  static RAM bytes. Its actual `firmware.bin` is 6,383,184 bytes, leaving
  170,416 bytes in the 6,553,600-byte OTA slot.
- The Nightly image is 6,336,496 bytes, leaving 217,104 bytes in that slot.
  The linker reports 110,816 static RAM bytes; dynamic heap still requires
  measurement on the device.
- Nightly packaging checks passed: ESP32-S3 chip/ReadPico board markers,
  partition layout, embedded version, asset sizes/checksums and matching
  `global` / `zh-CN` compatibility manifests. The local package is at
  `build/screen47-preview/nightly-package/`.
- The final native simulator rebuilt successfully. Main pages, both navigation
  positions and all four reader orientations were recaptured; saved 12pt/10px
  preferences remained intact. The Xvfb quit limitation recorded above remains
  separate from frame/dimension acceptance.

The package is a local verification build from the uncommitted working tree.
`LOCAL_BUILD.json` records the base commit, source fingerprints and binary hash;
the normal manifest commit/version suffix identifies that base, not a newly
published revision. No release has been published. That preceding build was
subsequently flashed; the serial smoke-test record is described below.

Overall layout is confirmed. The SVG chrome appearance and physical acceptance
remain pending.

## Previous build: physical serial smoke test

The connected ESP32-S3 ReadPico was flashed without backup at app0 `0x10000`,
using the existing matching partition table. Esptool verified the written hash.
The image was 6,336,496 bytes; SHA-256
`f6e212d4313cb3cadbda989491355910a8dda54c82bb0f6348665095d3048b76`.
Device-test artifacts are under `build/screen47-preview/device-test/`.

During the 40-second log capture, the 1216 × 684 display and SD initialized and
the saved book resumed; no current panic or OOM was observed. Internal free heap
was about 30–36 KiB, largest blocks about 10–13 KiB. CSS parsing was skipped
because the parser required 65,536 bytes, and startup emitted nine out-of-range
draw errors. The retained core-dump record also failed its boot checksum check
after the firmware replacement. These findings remain unresolved; this serial
smoke test does not establish visual, touch, or long-running acceptance.

## SVG chrome validation before existing-font replacement

The native simulator builds successfully. Six focused checks cover resource
size/polarity, deterministic SVG regeneration, production Tab drawing without
resampling, and battery fill/charging at 0/50/100% plus out-of-range telemetry,
with all four orientation transforms. The footer geometry check exercises
maximum 16-bit page counters, long titles, clocks on either side, battery
percentage hiding, and both auxiliary markers across four oriented screen
geometries. Actual generated glyph metrics check common Chinese, Latin
descenders, Hebrew and the larger estimate marker against the status line
when aligning the footer and reserving the confirmed bottom space. The status containment check uses the new 32 × 20 battery dimensions.
Font-lifecycle checks verify 12/14pt footer loading without replacing fixed UI
faces. Existing user font/margin preferences survive the native capture harness.

Native review captures use an isolated simulator test binary whose ignored HAL
stub copies temporarily accept battery/charging fixture values. The dependency
sources are restored after that build. The production preview binary and
hardware firmware contain no fixture controls. The captures cover all five
selected Tabs, battery states, percentage hiding in home and reading pages,
and four reader orientations. Native pixel checks match each generated Tab
asset in all five selection states, and match generated outline/fill/bolt pixels
for each battery state in both status bars and reading footers. The final
the then-current footer battery origin was `(38, 1175)` in portrait. Chinese/English boot and
light/dark default-sleep captures show separate title/status lines; the boot
version clears the safe bottom edge.


## Existing-font revision validation (2026-10-02)

The user confirmed the existing-font appearance, splash, 10px reading-footer
spacing and revised top battery arrangement. `major-pages-final-preview.png`
shows eight production SDL pages; `font-reuse-review.html` contains native-size
captures, all five Tab selections, battery states and orientations. The numeric
battery group now uses an 8px bitmap offset for the existing 12pt digits.
The right-aligned top group reserves 12px horizontal space; native visible
bounds for 50% and the battery are `(560,21)-(607,38)` and `(621,21)-(650,38)`,
leaving 13px visible space. Reader text retains its confirmed bottom gap.

The final normal ReadPico image is 6,113,344 bytes, with 440,256 bytes remaining
in its 6,553,600-byte app slot. The linker reports 6,108,135 Flash bytes and
110,808 static RAM bytes. This is 95,192 more linker Flash bytes than the
preceding SVG/custom-font revision (6,012,943); reusing existing raw CJK fonts
is not a Flash saving relative to that revision. Live heap is measured separately.

Full CI passed with formatting, default-target cppcheck, all eight hardware
builds, 817 host tests and SDK ReadPico display/resource/PMU checks. Focused
checks passed: 14 layout/cache regressions, six UI/font lifecycle checks, six
SVG/footer checks (including actual deterministic regeneration), and 32 Nightly
release checks. No fonts were regenerated. Shared font-converter and calculator
checks remain intact after removing the custom-font-only modifications.

The final native preview uses existing official NotoSansSC SD 12/14/16 resources,
validated against the project's download catalog sizes and CRCs. It shows “梦海”
complete with and without SD fonts; “霁海”, whose first character is outside the
common embedded set, uses SD fallback. Hebrew and shaped Arabic names and
Latin `gypjq` fit their rows. SD 14pt reading uses the installed face; offline
reading selects common CJK 12 while keeping saved `fontSize=14`. Selection after
the existing bidi conversion fixes the Arabic fallback without another visual
buffer. Bitmap and vector unload tests preserve built-in fallbacks, and the
vector path owns only one UI face per distinct size (12/14/16), sharing the
12/14pt instances with footer registrations.

Builds use an isolated `/tmp/crossmux-screen47-pio` core cache because another
worktree's PlatformIO run changed shared packages during the initial CI attempt.
The isolated rerun passed; temporary simulator battery hooks are restored and
excluded from production binaries. Nightly builds set `CROSSPOINT_RC_HASH` to
the verified base commit suffix, and packaging verifies that the embedded
version agrees with both compatibility manifests. No release is published.

The validated Nightly firmware is 6,066,624 bytes, leaving 486,976 bytes in the
OTA slot; its linker reports 6,061,415 Flash bytes and 110,784 static RAM bytes.
Version `1.6.5-readpico-rc+cfa0c36` agrees with both compatibility manifests.
SHA-256: `17e296db5f3295c2c78f328d3a4426b5ec001a210c78582119dd874885046f8b`.
The final portrait reader battery origin is `(38,1176)` and its visible center
matches the percentage digits; footer text still ends at row 1197, leaving
10px above the last visible row 1207. Physical bezel rows remain excluded.

## Existing-font revision: physical serial smoke test

The confirmed final Nightly image was written without backup to the connected
ESP32-S3 ReadPico at app0 `0x10000`. The OTA sequence/CRC identified that active
slot before flashing, and esptool verified the written data hash. Only the
application image was written; partition/NVS/SD contents were not erased.
Artifacts are in `build/screen47-preview/device-test-final/`.

A fresh 40-second serial capture confirmed the ReadPico board, 1216×684 panel,
SDMMC mount, external RTC and IMU initialization. It recorded zero current
Guru Meditation panics, OOM messages or out-of-range drawing messages.
Three post-startup samples at approximately 10/20/30 seconds reported internal
free heap 44,559 bytes (43.5KiB), maximum allocatable block 31,732 bytes (31.0KiB)
and boot minimum 12,184 bytes (11.9KiB). PSRAM free was 6,055,228 bytes (5.77MiB),
with largest block 6,029,300 bytes. The log includes an absent optional
`cover.override` file; the normal cover fallback remains available. The earlier
CSS low-memory skip was not present in this capture, which does not establish
CSS behavior on every book/page. This run's activity/cache state differs from
the preceding reader capture, so these measurements do not establish a general
memory or performance improvement. Serial startup does not prove physical
refresh quality, touch behavior or long-running stability.
