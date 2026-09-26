# SSD1677 text-only combined antialiasing

CrossMux enables combined text AA on Sticky, Murphy M4, Waveshare ePaper 3.97
and Metalio E-Ink4 (ESP32-S3, detected SSD1677 800 × 480, PSRAM available).
X4 Pro and X4 Classic default to the original B/W base plus gray overlay.
UC controllers, EEGO A4 and ESP32-C3 retain their prior paths. Paper Mono retains
its native combined driver and allocation-free SSD1677 fallback.

## Current default transitions

| Platform | Combined text AA | Trusted text transition | Image policy |
|---|---|---|---|
| Metalio E-Ink4 | on | on, including r2 entry counter | continuous cadence, white image cleanup |
| Sticky | on | on | original |
| Murphy M4, both SSD1677 batches | on | on | original, batch temperature retained |
| X4 Pro / X4 Classic | off | off | original |
| Waveshare 3.97 | on | on, added at user's request | original |
| Paper Mono / other devices | unchanged | unchanged | unchanged |

`FREEINK_SSD1677_READER_TRANSITIONS=0` restores prepass transitions while keeping
combined AA. `FREEINK_SSD1677_COMBINED_AA=0` restores the complete original route.
Defaults live in SDK BoardConfig and are inherited by normal/release/Nightly
builds. The Metalio diagnostic environments use these defaults plus logging;
the previous Metalio experiment flags are retired. No UI setting is changed.

The facade/HAL/renderer expose `supportsReaderTransitions()` and the separate
Metalio-only `supportsContinuousImageReading()`. Original drivers track physical
history independently of RAM cleanup and reject failed/unknown handoffs.
The shared EPUB/TXT helper preserves the r2 distinction between an unstarted
cycle (0) and actual periodic cleanup debt (1).

**Physical evidence:** the user confirmed the extra flash disappeared with
Metalio `1.6.0-metalio-transition-r2`. That confirmation concerns the reported
entry/first-turn symptom, not a completed 100-page ghosting test. Sticky,
Murphy M4 and Waveshare 3.97 have not yet received physical acceptance for this transition change.
The dated experiment sections below describe earlier states and are superseded
by this default matrix.

## Reader and image boundary

Only EPUB/TXT pages with text AA enabled, normal polarity, no images and no
reading background request `TextOnlyAntiAliasing` through GfxRenderer/HAL.
The SDK holds the B/W target until both current-generation grayscale planes
are complete, then performs one pixel activation. This removes the independent
B/W-body submission; it is not a guarantee that every panel's optical waveform
is invisible.

For the newly adapted boards, an image-containing page uses the original
SSD1677 driver for the whole transaction, including its text AA. Covers, sleep
images, XTC and other callers that do not request text AA keep their existing
path. No image LUT, two-plane encoding or book cache format changes. Paper
Mono's pre-existing image behavior is unchanged.

Switches discard pending gray data, finish outstanding work, invalidate the
old RAM baseline and use a trusted full-target AA handoff where supported;
unknown states retain correction. After controller idle sleep, the next text page
resets the controller before checking BUSY or writing RAM. Continuous text
pages do not switch drivers; continuous image pages do not gain an extra cleanup.
Manual/periodic cleaning and wakeup can still have a visible transition.
Missing gray data falls back to B/W; cancellation discards the page. BUSY
failure must stop register writes and preserve an unknown baseline until recovery.

## Board configuration and memory

`FREEINK_SSD1677_COMBINED_AA=0` disables the new routing at build time. This
switch does not remove Paper Mono's pre-existing driver. No settings page is
added. The ordinary environments and their release/Nightly derivatives
inherit the default; building these artifacts does not publish them.

`Ssd1677CombinedAa.h` contains independent calibration entries. Sticky retains
its verified 16/24/32-frame timing and voltages. The other boards start at the
same nominal 5 ms frame period and 16/24/32 counts, with voltage tails taken
from their original grayscale LUTs. These are first-round values, not optical
acceptance. Native scan direction and byte order follow the board orientation;
Paper Mono's mount transform is not applied to the other devices.

The original drivers perform the newly adapted boards' B/W fallback and
corrective refreshes. This retains Murphy's selected batch parameters,
Metalio's FAST 0xFC / FULL 0xF7 and black-pulse cleaning, and the corresponding
power policies. Metalio's combined path parks with 0x83. Sticky keeps its
existing F7 correction, separate C0 power settle, and gray waveform.

Eight 48,000-byte planes consume 384,000 bytes (375 KiB) in PSRAM. They carry
page and glass history across refreshes, so task-stack storage is unsuitable.
They are allocated once and reused, with no new full-page cache. Partial
allocation failure frees all eight slots and selects the original driver;
new optional allocations never spill into internal RAM. Serial logs identify
availability, allocation failure, path switches and BUSY timeout.

## Automated checks

```sh
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_sticky_combined_aa.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677_text_route.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/run_pro.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py
python3 scripts/tests/test_metalio_eink4.py
./bin/ci-check
pio run -e x4c -e metalio_eink4 -e sticky_aa_rollback
```

The route tests compile the actual SDK facade and original/combined drivers.
They compare complete image command/data traces, not just reported capability
flags. Cases include both Murphy batches, native/mirrored layouts, text staging,
missing planes, cancel, image/text switches, wakeup, each allocation failure,
and exclusion of the UC controllers. The combined-driver test injects BUSY
failure and checks that recovery cleans before resuming.

The default-transition matrix also checks the real BoardConfig defaults and
compile overrides, X4 Pro/Classic's original text command sequence without
combined buffers, RAM-only gray synchronization, asynchronous completion and
failed power-down during a driver handoff. CrossMux reader checks distinguish
entry counter 0 from due counter 1 and preserve manual/periodic correction.
Local build logs, firmware hashes and test results for the 2026-09-26 default
rollout are recorded in `build/reader-transition-defaults/VALIDATION.md` (local
artifact directory, not committed). These checks do not replace panel testing.

## Physical acceptance record

| Device / batch | Status for this revision |
| --- | --- |
| Sticky | User reported normal reading after the shared BUSY/driver-switch fix; this reviewed revision awaits retest |
| Paper Mono | Existing optical parameters retained; no new device measurement |
| X4 Pro / Classic, SSD1677 only | Awaiting physical acceptance |
| Murphy M4, both batches | Awaiting physical acceptance independently |
| Waveshare ePaper 3.97 | Awaiting physical acceptance |
| Metalio E-Ink4 | User confirmed the extra entry/first-turn flash disappeared with `1.6.0-metalio-transition-r2`; 100-page ghosting and full current-default optical checks remain pending |

The 2026-09-25 Metalio transition experiment used changed-pixel-only
text drive and delayed image cleanup. Host traces showed one text activation,
three activations on a scheduled text clean, and one versus two B/W activations
for continuous versus scheduled image bases. In follow-up, the user reported
blurrier text, unchanged visible flicker, and easy ghosting after testing the
changed firmware.
The affected page type and exact flashed image were not independently captured;
neither experiment is accepted, and both changes were reverted in source.
The local `build/metalio-reader-transition/` images and checksums are retained
as comparison records, not as an optical pass.

The subsequent `metalio_eink4_white_clear_experiment` keeps the known-good text
AA waveform and both endpoint-clean activations. It substitutes a white
intermediate screen for the black intermediate screen whenever Metalio's
BlackPulse policy runs, including initial/manual/recovery cleans. It is an
opt-in comparison build, not the default: final black density, image grays,
ghosting and the transition's appearance all need device inspection.
The same-source default and experiment images are in
`build/metalio-reader-transition/` with `WHITE_CLEAR_SHA256SUMS`.

### 2026-09-26: Metalio transition experiment

The earlier global white-clear image is retained as a historical comparison.
Current source scopes `FREEINK_METALIO_WHITE_CLEAR_EXPERIMENT` to EPUB image
bases marked `ImageReading`; text and menu clears use black. An image-base clean
uses white for all cleanup reasons, including initial/manual/recovery requests.
`metalio_eink4` keeps its existing combined-AA waveform and non-white drive.
The changed-pixel-only text experiment remains reverted.

`metalio_eink4_transition_experiment` adds `FREEINK_METALIO_TRANSITION_EXPERIMENT=1`:

- On a trusted original-driver B/W or gray → text handoff, stage the complete
  text target and submit the existing corrective AA waveform once, without the
  separate OTP endpoint-clean/base prepass. No LUT, voltage, decoder or permanent
  framebuffer is added. Host optical history is an estimate, not a measurement.
- The shared EPUB/TXT text-base helper selects this handoff and consumes the
  manual-refresh marker. Image cadence changes remain confined to EPUB.
- Original-driver history is sampled before sleep/reset. A power-down timeout
  revokes the handoff even when a subsequent reset releases BUSY. Unknown state,
  manual HALF/FULL, incomplete AA staging and recovery keep the original clean.
- Images use the existing page counter instead of forcing the next page to
  HALF. A due periodic clean at a trusted text handoff is deferred with the
  counter held at 1: the next ordinary text or image page pays it. Manual cleanup
  is never deferred. Leaving the reader still retains its normal correction.
- Only image bases use the white endpoint. Original image gray encoding/LUT
  and ordinary text AA drive remain unchanged. The experiment is default-off;
  no physical device was flashed or optically accepted in this iteration.

Expected **pixel-driving** activations (power-only 0xC0/0x83 excluded):

| Scenario | Default | Transition experiment |
|---|---:|---:|
| Trusted B/W → first AA text | 3 | 1 |
| Trusted image gray → AA text | 3 | 1 |
| Ordinary AA text page | 1 | 1 |
| Periodic/manual HALF text | 3 | 3 |
| Explicit FULL text | 2 | 2 |
| Unknown/fault-recovery FAST text | 3 | 3 |
| Synchronized FAST image base + gray | 3 | 2 |
| Due/image-entry clean base + gray | 3 | 3, white intermediate |

Host coverage exercises both builds, both handoff source types, rotated scans,
identical LUT bytes, unchanged eight-buffer allocation count, manual/full
requests, deferred cadence, cancellation, missing history and injected failures.
The 2026-09-26 host run passed 8 SDK test methods (including the board/scenario
matrix) and 2 parent reader/driver test methods. Physical results remain NOT_RUN.
Run `python3 -m unittest test_ssd1677.py test_ssd1677_text_route.py test_sticky_combined_aa.py`
from the SDK host-test directory and
`python3 -m unittest scripts.tests.test_metalio_display` from the parent root.
Build default, experiment and legacy-AA rollback with:

```sh
pio run -e metalio_eink4 -e metalio_eink4_transition_experiment -e metalio_eink4_aa_rollback
```

Experiment logs identify refresh context/mode/action and source state; text
activations log control byte, pixel-drive bit, BUSY result and elapsed time.
`SSD1677_PROBE_DEBUG` logs the original waveform stages. A reduction from three
activations to one **does not prove removal of the optical flash**: the existing
AA waveform contains an anti-target kick, which may remain visible.

For physical acceptance, compare complete fixed-exposure recordings of book
entry, image → text, consecutive text/images and the deferred clean; then compare
100 pages at identical font, orientation, temperature and refresh frequency.
Inspect black density, gray edges, image levels, background disturbance and
ghosting. Reject the candidate if any quality worsens or the unwanted full-screen
flash remains. Do not transplant the research documents' foreign-panel LUTs.
Model, USB identity and active OTA slot must be confirmed before any later flash;
the previously visible `/dev/cu.usbmodem101` was explicitly rejected as the test device.

For each available board, use the same book/font/settings for original versus
combined firmware. Turn 100 EPUB and 100 TXT pages; record first visible change,
final stability, intermediate image, residual ink, free PSRAM and largest free
block. Compare text → image → text, consecutive images, mixed text/images,
covers, backgrounds, menu return, rotation, night mode and sleep/wake.
Measure image decoding/cache-miss separately from cache-hit rendering. Record
switch-only cleaning separately from continuous-page timing.

Pass criteria: no independent B/W-body submission on ordinary AA text pages;
clear gray edges, normal white background, no accumulating ghosting; original
image levels and no clear cache-hit performance regression. Unmeasured or
unsatisfactory optical behavior remains explicitly pending calibration.

## Artifacts and rollback

The task's `build/ssd1677-text-aa/` package contains application binaries,
SHA-256 checksums, source revisions, build results and rollback instructions.
`sticky_aa_rollback` is the original Sticky path on the same source baseline.
Other new targets have matching rollback binaries in the package; its
`rollback-platformio.ini` preserves the original hardware flags and adds
`-DFREEINK_SSD1677_COMBINED_AA=0`. Rebuild it from this checkout with
`pio run --project-dir . -c build/ssd1677-text-aa/rollback-platformio.ini -e <target>`.
Only install a board-matching image after verifying device identity and the
active application partition. The Metalio test image was flashed only to the
verified device's app0 partition; no Nightly was published. No quantitative
optical acceptance is claimed from serial logs.

### 2026-09-26: first page-turn cleanup correction (r2)

Device feedback and serial logs showed that entry initialized the reader counter
at zero, but the transition helper treated it as periodic debt and left it at
one. The first actual page turn therefore ran the two-activation OTP cleanup.
For a trusted, non-manual text transition only, zero now starts the configured
reading cycle after the entry page. A genuinely due counter of one still defers
and retains cleanup debt. Unknown sources and manual refresh retain correction.
No text LUT or normal page waveform changed. Host regression covers entry,
first turn, scheduled cleanup, unknown source and manual entry. A frequency of
one still intentionally cleans on every following page.

The first candidate was flashed and its handoff path verified on Metalio
10:20:BA:6E:08:70; optical acceptance failed on the first page turn as reported
by the user. r2 requires a new physical check of that sequence.
