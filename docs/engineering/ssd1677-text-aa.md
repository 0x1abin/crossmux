# SSD1677 text-only combined antialiasing

CrossMux enables three-tone combined text AA by default on Sticky, X4 Pro,
X4 Classic, Murphy M4, Waveshare ePaper 3.97 and Metalio E-Ink4 when the build
is ESP32-S3, the detected controller is SSD1677 (800 × 480), and all eight
PSRAM buffers can be allocated. UC8179/UC8279 batches, EEGO A4 and ESP32-C3
retain their previous paths. Paper Mono retains its existing combined driver;
allocation failure now selects the allocation-free SSD1677 fallback.

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
old baseline and clean once. Continuous text pages do not switch drivers;
continuous image pages do not gain an extra cleanup. Manual/periodic cleaning
and wakeup can still have a visible transition. Missing gray data falls back
to B/W; cancellation discards the page. BUSY failure must stop register writes
and preserve an unknown baseline until recovery.

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

## Physical acceptance record

| Device / batch | Status for this revision |
| --- | --- |
| Sticky | Previous `sticky-aa-test2` reported normal by the user; this rebased/default-enabled revision awaits retest |
| Paper Mono | Existing optical parameters retained; no new device measurement |
| X4 Pro / Classic, SSD1677 only | Awaiting physical acceptance |
| Murphy M4, both batches | Awaiting physical acceptance independently |
| Waveshare ePaper 3.97 | Awaiting physical acceptance |
| Metalio E-Ink4 | Physical testing remains paused |

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
active application partition. This task does not flash hardware or publish
Nightly; no new quantitative/optical acceptance is claimed from serial logs.
