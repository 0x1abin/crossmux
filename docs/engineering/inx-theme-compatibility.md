# INX SDK layout compatibility

INX preserves the visual contract of CrossMux 30db80e3 / FreeInk SDK 094976e1.
The shared bridge in `src/components/UIThemeTokens.h` selects `ThemeRow` list
layout and advance-based digit alignment when `FREEINK_UI_THEME_LAYOUT_POLICY`
is available. Older SDKs retain their original behavior without these fields.
Do not add per-activity row-height workarounds: direct callers such as the
end-of-book suggestion menu must use the same theme resolution as paginated lists.

`KeyboardEntryActivity` selects Classic geometry and the legacy key tables only
for INX. It explicitly supplies the original spacing and square corners.
New Arabic keys have no legacy layout and keep their upstream arrangement.
Other themes retain the SDK defaults. Existing popup properties already specify
INX fonts, alignment and spacing; avoid redundant overrides.

Clipping, provider access, atomic navigation and measured-height pagination stay
active. A short section header may allow another complete row; long wrapped rows
use the corrected scrollbar estimate. These are pagination boundary fixes, not
reasons to revert the upstream UI implementation.

Run `python3 test/inx_navigation/test_inx_style_compat.py` for component draw/hit
parity and boundary checks. To regenerate the reference, pass an SDK checkout
at 094976e1 and review the JSON output before replacing the baseline file.
The harness uses fixed font metrics, not whole-screen physical snapshots.
Physical acceptance must cover end-of-book menus, settings, libraries, popups,
keyboard, scales and orientations separately from display ghosting/sleep tests.
