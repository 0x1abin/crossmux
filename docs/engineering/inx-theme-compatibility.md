# INX SDK layout compatibility

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

The reviewed non-INX reference is Reader
`93e98bb78702e29868a16a13b80c40e6b36ccdff` with SDK
`5deb923c33d97f918227b2ad983fda4d5a63c6ae`. Existing upstream menu entries retain
their category, relative order and visibility conditions. Shared CrossMux settings
remain visible in every theme at the same category and relative insertion point,
with their existing action/value binding. This is a structural position, not an
absolute screen coordinate. Keep one shared settings source, persistent fields,
Web API, CrossMux update service and device capability checks.

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

### Fixed-snapshot checkbox increment

This rehearsal stays on CrossMux `9d02f498`, Reader `93e98bb`, SDK `5deb923c`
and Simulator `8699595`. The approved Reader `d1509d0` and SDK `e41f683e`
checkbox increments are applied separately. Non-INX boolean controls use their
upstream checkbox rendering and hit geometry, including reader overlays; INX
retains historical reader-menu text states; settings controls follow the explicit exception below. Multi-valued paragraph spacing, synthetic bold
and reading-guide options remain selectors. Reference captures use the same
fixed snapshots plus these increments. Latest main/upstream updates are a
separate task; no historical INX baseline is rewritten.

The shared large UI font (`NOTOSANS_18_FONT_ID`, including slider readouts) uses
upstream Noto Sans 18 regular/bold outside INX; INX keeps its historical offline
fallback. The existing theme reload updates stable font objects, without adding
reader font choices or per-page allocations. Date/time and reading-statistics
boolean extension settings use shared checkboxes in all themes. Legacy extension
lists route through the SDK list component; their fixed single-line rows publish
the same cadence consumed by touch and button pagination. Test this bridge with
`test/inx_navigation/test_legacy_list_adapter.py`, including the last page and gaps.

For English whole-page comparisons with paragraph spacing disabled, select
CrossMux first-line indent = Indent (1): this corresponds to upstream's implicit
three-space first-line indent. Auto (0) is a retained CrossMux setting and has
intentionally different semantics. Keep INX comparisons on their original settings.

### Approved INX setting-control exception

Boolean setting rows now use the shared 28px SDK checkboxes, including the INX
accordion and settings subpages. Slider adjustment dialogs use Lyra control
geometry and fixed upstream 10/12/18pt font bindings. Keep INX category layout,
row cadence, page chrome, ordinary option pickers and reader menu/toolbar text.
Historical baselines remain immutable: old setting-control scenes demonstrate
legacy SDK compatibility; current entrypoints are checked separately against
the uniform controls. Do not interpret this as approval to restyle whole pages.

The three control font families share existing bitmap data; registration adds
three persistent font-map entries at startup, avoiding per-dialog allocations
or mutation of the INX page fonts.
