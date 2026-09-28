# SSD1677 text-only antialiasing

The reader submits its complete B/W target and both gray planes through the
existing renderer/HAL intent. The SDK owns eligibility, waveform selection,
controller power and committed screen history. EPUB/TXT normal-polarity pages
with no images or reading background use this path. Images, covers, inverted
pages, manual/periodic cleanup and unknown-history recovery retain their
existing driver and cleanup policy.

## Platform defaults and candidates

| Platform | Combined AA | Endpoint-preserving text turn | Candidate build |
|---|---|---|---|
| Metalio E-Ink4 | default | default; user confirmed synchronous AA and fading fix | `metalio_eink4_transition_experiment` (diagnostics) |
| Sticky | default | opt-in pending panel acceptance | `sticky_text_aa_candidate` |
| Murphy M4, both SSD1677 batches | default | opt-in pending acceptance for each batch | `murphy_m4_text_aa_candidate` |
| Waveshare ePaper 3.97 | default | opt-in pending panel acceptance | `waveshare_epaper_397_text_aa_candidate` |
| X4 Pro / X4 Classic | off | off | original B/W + gray overlay |
| Paper Mono, UC controllers, EEGO A4, ESP32-C3 | existing behavior | unchanged | existing environments |

Normal/release/Nightly inherit the defaults. Candidate builds use the same
shared core with the selected board's voltage tail, border, scan orientation,
sleep key and power-off control. Murphy's original-driver batch selection and
temperature policy remain intact. These panels' independent calibration entries
are retained; equal initial frame counts do not establish equal optical results.

`FREEINK_SSD1677_TEXT_TURN_AA=0` restores the previous balanced text waveform
without disabling combined staging or trusted handoffs. Setting it to 1 opts
other combined-AA boards into the new text turn. `FREEINK_SSD1677_COMBINED_AA=0`
restores the original driver, and `FREEINK_SSD1677_READER_TRANSITIONS=0` restores
prepass handoffs. X4 Pro/Classic remain excluded by default. No settings or public
API are added. The earlier `FREEINK_METALIO_TEXT_EDGE_*` experiment is retired.

## Final text-turn design

`PaperMonoDriver::runUpdate()` now owns one submission/completion flow for
legacy, safe and endpoint-preserving AA. `makeTextTurnLut()` only generates the
waveform; it does not write the controller or advance screen history. Both LUT builders write directly into the
submission buffer, avoiding a second temporary LUT and its copy. This
removes the Metalio-specific duplicate update routine.

On a changed page, selector entries are idle (0), target white (1), changed
gray (2) and target black (3). Every target-white and target-black pixel drives,
including static status text, guides and overlapping body strokes. Static gray
selects idle: resetting it to white every turn would visibly redevelop AA.
The logical change mask independently gates submission, so identical pages do
not refresh. Neither idle nor VCOM receives compensation.

The accepted Metalio timeline is:

| Class | Frames | Direction |
|---|---|---|
| Changed gray | `[0,32)` then `[32,56)` | white preparation, then weak gray development |
| Target white | `[24,56)` | white only |
| Target black | `[24,56)` | black only |
| Static gray / VCOM | entire waveform | zero drive |

There is one pixel activation, with no separate B/W prepass, anti-target kick
on black, post-refresh gray pass or additional cleaning for ordinary turns.
Rail settling (`0xC0`) is power-only; the custom pixel trigger is `0x0C` and
must not load OTP, which would replace the custom LUT. Metalio parks with
`0x83`; Sticky, Murphy and Waveshare retain their own power-off controls.

`Ssd1677CombinedAa.h` owns per-panel `Calibration` and `TextTurnTiming`. Gray and
black lengths reuse that board's calibration; preparation, endpoint start and
white duration are independent fields. Bounded build overrides
`FREEINK_SSD1677_TEXT_WHITE_FRAMES=32/40/48` and
`FREEINK_SSD1677_TEXT_BLACK_DELAY=0/8/16/24` are available for calibration.
The other boards' candidate starts at Metalio's nominal 32+24/32/32 timing,
with their original analog parameters. No foreign-panel voltage set is imported.

Eligibility requires a completed, trusted baseline, FAST text intent, normal
polarity and no corrective or separately displayed B/W overlay. First entry,
image return, power-cut sleep and timeout recovery use the safe waveform.
Idle controller sleep retains the glass model and resets the controller before
reuse. Complete current-generation gray coverage is required; missing/obsolete
planes cannot become a partial AA target.

Cancellation can discard work before the pixel trigger, including during rail
settle. After submission, the page finishes as a whole. BUSY timeout stops
writes and invalidates the baseline; the target is recorded only after successful
completion. Existing manual/periodic cleaning and reader cadence remain intact.

Eight 48,000-byte planes (384,000 bytes / 375 KiB) are allocated once in PSRAM
and reused. Allocation failure frees partial storage and selects the original
driver. The bounded stack LUT and boundary list use 111 and 14 bytes; no per-page
allocation, extra full-screen cache or dependency is introduced.

## Review and automated verification

The review retained the successful final waveform and selector policy, removed
the duplicate Metalio power/submission/state routine and unreachable post-clean
experiment, consolidated calibration
ownership, replaced historical experiment flags with one shared gate and
updated tests to exercise all four platforms. Legacy/corrective behavior is
kept as the safe path, rather than layering another refresh over the text turn.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p test_metalio_display.py
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/tests -p test_ssd1677_text_aa.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/run_pro.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py
./bin/ci-check
pio run -e metalio_eink4 -e metalio_eink4_transition_experiment -e gh_release
pio run -e sticky_text_aa_candidate -e murphy_m4_text_aa_candidate -e waveshare_epaper_397_text_aa_candidate
```

Recording-bus tests cover all nine W/G/B transitions, static black maintenance,
static gray idle, white compensation, identical-page suppression and full
frame-by-frame LUT decoding. They exercise all 12 bounded white/delay choices,
100 successive changed/repeated targets without additional allocations,
pre/post-submission cancellation, rail/pixel BUSY failure, recovery, parking,
sleep, cleanup and original image routing. The platform matrix covers both
Murphy batches, native/mirrored scan, UC exclusion, rollback and Metalio's new
default. Reader tests preserve cleanup debt and manual/periodic refresh cadence.
Software tests do not measure pigment behavior.

## Physical evidence and rollout

The user tested Metalio `1.6.0-metalio-aa-g24-b32-w32-d0-khold` and confirmed the
reported fading problems were resolved; the preceding candidate had already
met synchronous-AA expectations. Its SHA-256 is
`cf5649adbf31e4333f67dd693e52510559d397de9f9b429c3b2e545ada3553d1`.
Source snapshots, prior images, hashes, upload and startup evidence are retained
locally under `build/metalio-aa-calibration/`. The independent 100-page/video
record is not available; do not present user feedback as a measured endurance
or latency result. Other platforms' new waveform remains opt-in.

For each panel/batch, start with manual cleanup, fix book/font/orientation,
lighting and cadence, and compare at least 20 fixed-page round trips followed
by at least 100 pages including CJK/Latin, small/bold fonts, back/forward turns
and dwell. Check status text, guides, body density, gray edges, simultaneous
appearance and residual ink at the unchanged cleanup interval. Check image
return, inversion, periodic/manual cleanup and sleep/wake independently.
Reject visible AA redevelopment, faded endpoints or worsening accumulated
residue; do not hide failures by increasing cleanup frequency.

The previously rejected two-activation B/W-then-gray structure and subsequent
changed-pixels-only endpoint variant are superseded by this design. Do not
restore them as fallback candidates. For later hardware installation, verify
board identity and active OTA slot and write only the matching application.
Publishing a PR or building a candidate does not establish its panel acceptance.
