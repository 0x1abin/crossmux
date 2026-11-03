# INX SDK layout compatibility

## Approved settings groups (2026-10-23)

All themes, including INX, use the upstream **System**, **Display**, **Reader**,
**Controls**, **Library**, and **Connectivity** groups. This is the explicitly
approved exception to earlier INX category organization. Keep CrossMux extension
settings in their corresponding groups through the same data source, with their
relative insertion points, action/value bindings, persistence, Web API, and
capability checks intact. Time to Sleep belongs to Display; Clear Reading Cache
to Reader; Wi-Fi, Bluetooth, OPDS, sync and Plugins to Connectivity.

INX retains its accordion presentation, page chrome, fonts, row cadence,
separators, ordinary pickers and theme-specific setting controls. This category
exception does not authorize other appearance changes. Historical four-category
captures, latency records and visual baselines below remain unchanged and tied
to their original images; validate all six current groups separately.

## Shared behavior across themes

INX retains its fonts, rectangular switches, dotted separators, main tabs,
accordion and cover layouts. Setting execution, option selection, input ownership,
persistence and navigation use the same owners as other themes. The Library main
tab continues to browse directories; the shared Apps catalog also exposes the
indexed Library through stable app ID 17. Existing app IDs and masks keep their meaning.

On touch hardware, all five INX main tabs (Recent, Library, Apps, Settings, Statistics) start
without an established item selection, in both list and icon layouts. Valid row
taps/long presses establish selection and execute immediately; blank/cancelled
touches and scrolling never create one. Selection feedback joins the action's
result render, without painting the outgoing page. Existing page instances keep
selection and scroll after a popup or child activity returns. Reloaded directories
clear it; a renamed or returning entry is restored only if it still exists. Successful
Recent/Statistics removal clears the operated selection. Pickers, statistics subpages,
other themes and button-only devices keep their behavior.

With no selection, the first Next selects the first item and Previous the last.
The first Confirm selects the first item in Library/Apps/Settings, but the currently
visible entry in Recent/Statistics, including after an operated entry scrolls away.
Another Confirm executes. Consume the complete initial gesture, including holds and all held-key
releases, before normal navigation resumes. Inherited holds cannot establish a
selection. A fixed-size page flag and one frame snapshot drive both font scanning
and painting; hidden selections clear unsatisfiable row-follow work. No global
input-mode tracking, heap allocation or persistent selection history is added.
Main tabs still highlight the current tab and retain actual Tabs/Content focus.
Established selection remains visible when using touch. Option
pickers share press feedback, release activation, cancellation and a bounded visible
window; options beyond index 15 remain reachable. Headline and message dialogs use
the shared content renderer in every theme.

Recent Flow accepts horizontal swipes only when they start in the same cover band
used by drawing. Left advances one book, Right moves back, and both stop at the
ends; vertical paging remains available. Cancel the old touch press before paging,
without establishing a new selection or refreshing the outgoing page. Global edge,
Home and control-center gesture routing still runs first. Statistics keeps its
initial book/overview display and decorative borders, tracks browsing separately
from the operated entry, and adds selection emphasis only to the selected cover or
overview grid. Missing covers load for the visible page even without a selection.

Settings layouts pass the actual SettingInfo to one executor. Ordinary values save
without reconstructing the lists. Accordion changes only rebuild presentation rows.
Resource discovery runs on entry and resource-management return; structural rebuilds
reuse the dictionary setting. Recent skins share the store and removal controller.
Grids use the list host's viewport and published touch actions. About takes one
hardware/runtime snapshot and displays SD loading before querying space.

When main tabs own focus, the list host clears pending row-follow work. Hiding a
row highlight while leaving that work pending makes the SDK report the selection
as undrawn, which can exhaust the host's eight additional drawing passes without
advancing the viewport. Content focus and genuine wrapped-row follow still work.

Run the current production behavior checks alongside the historical drawing checks:

```sh
python3 test/inx_navigation/test_theme_menus.py
python3 test/inx_navigation/test_touch_network.py
python3 scripts/tests/test_reading_ui_regressions.py
python3 scripts/tests/test_about_layout.py
python3 scripts/tests/test_app_visibility.py
```

Historical SDK baselines stay unchanged. Current-entrypoint tests cover nonzero option
highlights, absolute indices beyond 16, shared file/recents commands, statistics hit
targets and held-entry releases. Compare drawing geometry separately from business actions.

### Refactor validation boundary

The 2026-10-17 cleanup consolidates statistics input into a page-local three-state
result and prewarm ownership into its scan enum plus nullable owner. It preserves
the selection snapshots, gesture priority, cache limits and persisted formats.
SDK scan protocol checks run in `freeink-sdk/libs/ui/FreeInkUI/test/host/run.sh`;
CrossMux retains page, viewport and font-cache integration checks.

Metalio's completed five-tab, Flow swipe and statistics cancellation acceptance
predates this cleanup. Its final application SHA-256 is
`412c04d72d3fc7077d1687094681c64a02fa65e0d61e94ddfeefe142f4d26d0a`.
Keep that record tied to its image. The cleanup has software/build verification
only; Metalio, Sticky and X3 require separate physical acceptance of the new image.
No device is reflashed as part of this refactor.

### Cross-device settings latency acceptance

Use identical language, font files, SD contents and orientation for reference and
candidate. The development `sticky` environment enables debug logs. Capture each
category's first expansion and 20 subsequent expand/collapse operations, including
multiple open categories. Record handler, drawing and panel wait separately:

- `LIST ... touch down` / `touch release` timestamp the shared input snapshot.
- `SET Category ... handler=... lockWait=... ms` separates render-lock waiting from
  rebuilding accordion rows; expansion must not discover resources or save settings.
- `LIST ... scan=... paint=... display=... ms` includes glyph collection/prewarm,
  the final paint, and synchronous display submission as separate intervals. The
  scan never publishes a hit table or dispatches a gesture. The actual paint must
  publish the final table once, after any wrapped-row viewport convergence.
- `GFX Time = ... from clearScreen to displayBuffer` measures only the final
  drawing pass after the last clear. Rebuild passes reset that timer; use the
  complete settings render interval and a separately timed display submission to
  measure all drawing work. Font statistics labeled `settings` identify repeated
  decompression and SD reads.
- `SET Settings render including display` includes synchronous display work; SDK
  `EPD Wait complete: refresh` logs isolate panel BUSY time. Enable the existing
  `SSD1677_PROBE_DEBUG` build flag if the actual refresh mode needs confirmation.

Record first-operation time, repeat median/p95, refresh count, font reads/decompressions,
internal free heap and largest block. Confirm one panel submission per expansion,
no dropped taps after publication and no decreasing heap across repetitions. Builds
and host tests do not establish physical latency or waveform quality; complete this
acceptance record independently for each accessible device before claiming a speed
improvement. Build all public hardware targets plus X4 Classic and LilyGo T5 S3;
classify unavailable hardware as build/host-verified, not physically accepted.

The earlier local 2026-10-16 render-only acceptance used five open/close pairs per category plus all
four categories open together, in isolated reference/candidate images. The probe
forces runtime INX/content focus and an awake CPU, calls production row activation,
and waits for each real display submission before the next action. Its display
interval includes transfer and BUSY waiting; it does not separately measure the
electrical BUSY signal or capacitive-touch recognition. Logs, image hashes and the
restoration record are retained in `build/inx-unification/validation.md`.

With the unchanged built-in Chinese UI fonts and no external font/dictionary
families installed, repeat render medians (four repeats after the first open) were:

| Category | Reference (ms) | Candidate (ms) |
|---|---:|---:|
| Display | 4328 | 1119 |
| Reader | 3649 | 1040 |
| Controls | 3594.5 | 1056 |
| System | 3919 | 1034 |

Both used reference HEAD `cfdf9825` and SDK `bb41a568`; the candidate adds the local
INX unification changes, before the subsequent shared UI CJK prewarm fix.
Each completed 48 activations. The display interval was
677 ms in both runs. Removing eight redundant drawing passes reduced render time
by about 71–74%, without changing font cache budgets. These are render intervals,
not touch-to-panel latency. The first candidate dispatch also waited 749 ms for a
queued warm-up frame; candidate timings are paired from the executed category/mask
log to its unique subsequent render, excluding that earlier frame.

## Historical appearance and integration contract

INX preserves the visual contract of CrossMux 30db80e3 / FreeInk SDK 094976e1.
The shared bridge in `src/components/UIThemeTokens.h` selects `ThemeRow` list
layout and advance-based digit alignment when `FREEINK_UI_THEME_LAYOUT_POLICY`
is available. Older SDKs retain their original behavior without these fields.
Do not add per-activity row-height workarounds: direct callers such as the
end-of-book suggestion menu must use the same theme resolution as paginated lists.

The keyboard is an explicitly approved exception to the historical INX appearance:
`KeyboardEntryActivity` uses the same SDK default key tables, separated geometry,
row spacing, rounded corners and ink-bounds centering in every theme. This includes
Spanish accent hints and long-press output; the former INX Spanish-table exception
no longer applies. Text, password, URL, symbol and URL snippet keys share this
styling. Page headers, text fields and button hints retain the active theme.
Existing popup properties already specify
INX fonts, alignment and spacing; avoid redundant overrides.

INX alone keeps the UI_10 body font, theme-grid list sizing, emphasized/value
styles, legacy scroll inset and separator/selection geometry. Other themes use
the reviewed upstream UI_12 body font and list defaults, including the small
value font, button-device minimum row height and one safe-area compensation.
SDK ThemeRow-specific selected/dimmed styling must not affect Content layouts.
Lyra Carousel retains its fork-only cover home and inherits Lyra's shared page
rendering, including list styling; only its home metrics differ from Lyra.

The historical local integration reference (2026-10-02) is Reader
`38280863a50988683c2c63ccd7096e872a48c411`, SDK
`98b4e427459bc588fdcdcb25e1454f0806cd806b`, and Simulator
`20e738038605ff4ead6c9fcfe932443d1e143691`, on CrossMux `593c8dbc`.
The SDK integration retains all six ReadPico commits through `e3550ec`;
rehearsal source-tree fingerprints describe staged resolutions separately from commits. Existing upstream menu entries retain
their category, relative order and visibility conditions. Shared CrossMux settings
remain visible in every theme at the same category and relative insertion point,
with their existing action/value binding. This is a structural position, not an
absolute screen coordinate. Keep one shared settings source, persistent fields,
Web API, CrossMux update service and device capability checks.


The local rehearsal dated 2026-10-13 uses Reader
`ba39565ce7b3a8aab14ca86da4fa4c3ca140f007`, SDK
`7fcd3b34c108f28c086ade3b433924022dbc0998`, and Simulator
`1915ee0a15564c3f49d74fa2334f773f282c5364`, on CrossMux
`83b0f810c8628701456e028c2d8566a9be149755`. Record reviewed index
fingerprints separately; these candidates contain no new commits.

The user approved retaining the compact offline reading-font aliases in
`src/main.cpp`: the legacy Noto Sans/Serif reader IDs share the local CJK12
fallback, and complete families/styles remain available through SD `.cpfont`
files. This is a reading-body and font-choice exception in every theme;
shared menu fonts and geometry still follow their existing contracts. On the
fixed English comparison book, Normal spacing yields 34 px / 8 pages locally
and 32 px / 7 pages with upstream's default Latin Noto Sans 12. Preserve both
captures and the historical baseline. Also compare both programs with the same
actual SD font, settings, content, language, orientation and scale; record any
remaining text-layout difference and its source instead of substituting fonts
or claiming that default reading pages match upstream.


Same-SD-font captures also retain the local per-glyph advance rounding in
`GfxRenderer::getTextAdvanceX`; upstream rounds the accumulated fixed-point
word advance once. This can change a boundary wrap even with an identical font
file and page count. The 2026-10-13 portrait and landscape SD-font captures
match the actual CrossMux `83b0f810` reading pages pixel for pixel. Record the
font file hash and production paths with each comparison; do not replace this
retained reading behavior solely to obtain an upstream pixel match.

Home adds APP to the upstream entries; Carousel's home remains the approved
exception. Only INX shows the recent, library and apps layout settings. All
settings subpages, reader/library menus, selectors and popups use the current
theme's components. INX-specific fonts, alignment and geometry must not leak
into other themes. No new theme framework or public API is needed.

`python3 test/inx_navigation/test_theme_menus.py` checks production menu assembly,
action bindings and shared tab drawing/hit regions against the fixed upstream
reference. Component checks do not replace full-page comparisons on identical
devices, content, language, orientation and scale, or hardware acceptance. Never
rewrite the historical INX baseline to make a changed page pass.

Clipping, provider access, atomic navigation and measured-height pagination stay
active. A short section header may allow another complete row; long wrapped rows
use the corrected scrollbar estimate. These are pagination boundary fixes, not
reasons to revert the upstream UI implementation.

Run `python3 test/inx_navigation/test_inx_style_compat.py` for component draw/hit
parity and boundary checks. To regenerate the reference, pass an SDK checkout
at 094976e1 and review the JSON output before replacing the baseline file.
The harness uses fixed font metrics, not whole-screen physical snapshots. Its
legacy keyboard scenes only check SDK backward compatibility, not the active INX
keyboard. Run `python3 test/inx_navigation/test_unified_keyboard.py` for the actual
activity's shared keyboard setup, layout, drawing and hit-region checks.
Physical acceptance must cover end-of-book menus, settings, libraries, popups,
keyboard, scales and orientations separately from display ghosting/sleep tests.

For upstream sync, run `test/inx_navigation/test_upstream_theme_compat.py` with
`--sdk` pointing to the reviewed fork candidate, `--upstream-sdk` pointing to the
reviewed SDK snapshot, and `--upstream-reader-ref` identifying the reviewed
Reader commit available in this checkout. It compiles the real theme bridges
with each side’s actual production metrics and fixed font measurements, comparing draw/hit traces
for Classic, Lyra, RoundedRaff, Lyra 3 Covers and Cover Grid's shared components.
It also compares the working INX bridge with pre-sync HEAD. The dedicated home
renderers, theme implementations, full-screen snapshots and physical devices
need separate acceptance; these component checks cannot establish that parity.


## Cover Grid and local integration acceptance

Persisted theme IDs are Classic 0, Lyra 1, Lyra 3 Covers 2, RoundedRaff 3,
Carousel 4, INX 5 and Cover Grid 6. Append new IDs; never renumber old values.
Cover Grid shares Lyra's non-home pages and adds APP after the upstream home
entries. OPDS visibility changes both the tab icons and their action indices.
Without PSRAM, hide Cover Grid from selection and display/render Lyra, while
retaining saved ID 6 until the user chooses a theme. Allocation failure must
leave a usable fallback; close the grid/cache when its activity exits.

Non-INX UI_10/UI_12 faces bind to the regular and bold upstream Ubuntu data.
INX also uses those same Regular/Bold faces as a user-approved appearance exception,
while retaining its font-size choices and independently pinned metrics. Glyph
weight, width, kerning and wrapping may change; default Base/Lyra geometry must
not change INX. The four shared Ubuntu assets in `lib/EpdFont/builtinFonts/` are
byte-for-byte assets from Reader 93e98bb; do not keep a second Medium/Bold set or
separate slider registrations at 10pt/12pt. The large-text and Chinese fallbacks
remain unchanged. Keep historical baselines and record font-induced page
differences explicitly. Shared pages use upstream small label/value text. CrossMux-only
workflows (APP, transactional font install, update channels/release notes) use
the active theme and preserve their functional bindings.

`sync-upstream start --local-rehearsal` keeps SDK → Simulator → Reader review
order but exports each reviewed dependency index for local integration. Record
base/upstream commit IDs and exported source fingerprints with the firmware;
the parent gitlink alone does not describe a rehearsal build. These candidates
cannot be published. Publication requires real dependency commits, a fresh
validation and explicit authorization.

Use separate SD caches for candidate/reference screenshots: book and section
cache versions differ, even with identical EPUB/content and settings. Compare
actual Sticky programs at equal language/orientation/scale; record shared
extension rows separately from upstream rows and confirm their hit regions.
A passing component trace or build is not whole-page acceptance. Preserve all
historical INX baselines and record physical verification independently.

### Historical fixed-snapshot checkbox increment

The previous rehearsal used CrossMux `9d02f498`, Reader `93e98bb`, SDK `5deb923c`
and Simulator `8699595`. The approved Reader `d1509d0` and SDK `e41f683e`
checkbox increments are applied separately. Non-INX boolean controls use their
upstream checkbox rendering and hit geometry, including reader overlays; INX
retains historical reader-menu text states; settings controls follow the explicit exception below. Multi-valued paragraph spacing, synthetic bold
and reading-guide options remain selectors. Reference captures use the same
fixed snapshots plus these increments. Current integration pins are recorded above; no historical INX baseline is rewritten.

The shared large UI font (`NOTOSANS_18_FONT_ID`, including slider readouts) uses
upstream Noto Sans 18 regular/bold outside INX; INX keeps its historical offline
fallback. The existing theme reload updates stable font objects, without adding
reader font choices or per-page allocations. Date/time and reading-statistics
boolean extension settings use INX switches and shared checkboxes in other themes.
Legacy extension lists route through the SDK list component; their fixed single-line rows publish
the same cadence consumed by touch and button pagination. Test this bridge with
`test/inx_navigation/test_legacy_list_adapter.py`, including the last page and gaps.

For English whole-page comparisons with paragraph spacing disabled, select
CrossMux first-line indent = Indent (1): this corresponds to upstream's implicit
three-space first-line indent. Auto (0) is a retained CrossMux setting and has
intentionally different semantics. Keep INX comparisons on their original settings.

### Approved INX setting-control exception

INX boolean setting rows use the SDK's original 38x18px rectangular switches,
including the accordion and settings subpages. Off places the square knob on the
left; on places it on the right, with colors inverted on selected rows. Other
themes retain the shared 28px SDK checkboxes. Multi-select lists such as keyboard
layouts retain checkboxes in every theme; their locked default row keeps its
existing text state. Slider adjustment dialogs use Lyra control geometry and
fixed upstream 10/12/18pt font bindings. Keep INX category layout,
row cadence, page chrome and ordinary option pickers. Reader menus and the
control center follow the explicit upstream exception below.
Historical baselines remain immutable: old setting-control scenes demonstrate
legacy SDK compatibility; current entrypoints are checked separately for the
theme-specific boolean controls and uniform sliders. Do not interpret this as
approval to restyle whole pages. Run `test/inx_navigation/test_checkbox_rows.py`
for boolean state, switch/checkbox drawing and hit geometry checks, and
`test/inx_navigation/test_inx_setting_controls.py` for sliders.

The three control font families share existing bitmap data; registration adds
three persistent font-map entries at startup, avoiding per-dialog allocations
or mutation of the INX page fonts.

## Touch and high-density integration

Only ReadPico hardware, Nightly and its simulator opt into `UiHighDpiProfile`.
Resolution alone never enables it. Existing font bindings and fallback chains stay
in place. Keyboard drawing and input use the same panel geometry: 96px keys and
12px gaps are preferred; ReadPico landscape retains one cursor-visible input line
and may reduce gaps to 6px so primary and alternate labels fit. Portrait keeps two
input lines. Regular INX keyboard geometry follows Lyra while its page chrome stays INX.

INX option pickers share `InxOptionGeometry::layout` for drawing and hit regions.
The safe area determines the visible rows; vertical swipes move by that actual row
count, clamp at either end, and consume their release without selecting an option.
Touch-down does not move the viewport under the finger. WiFi scan, connection,
failure and list states publish Cancel/Retry/Show networks controls through the
existing FreeInkUI frame, with at least 6px between buttons and bounded wrapped
status text. Simulator checks and screenshots are separate from physical touch,
refresh and sustained reading acceptance.

### Reader menu and control center exception

For the fixed Reader `38280863a50988683c2c63ccd7096e872a48c411`, every
theme uses the upstream reader menu assembly, toolbar, lists, checkboxes,
option dialogs and quick actions. INX uses the Lyra counterpart for these
surfaces and their chapter/bookmark/percent/footnote entrypoints only, including
font bindings and text alignment; its ordinary settings
pickers and other pages retain their own geometry. The Text panel replaces
Focus Reading with First Line Indent (Auto / Indent / No indent), retaining
the existing setting and default. A selection persists and reflows the page
under the panel. Image scaling remains available in the regular settings.

The top-edge down swipe opens the same control center on touch boards with or
without a frontlight. ReadPico therefore shows the upstream night-mode, refresh,
orientation and touch-control tiles, while capability checks omit unavailable
light sliders. These surfaces keep the explicit high-density control sizes and
board safe area. Historical INX traces are not rewritten for this exception.
