# Read Pico (小纸 Pico, RDP-G01-W)

The confirmed 4.7-inch UI is documented in [high-dpi-ui-profile.md](high-dpi-ui-profile.md). Its high-density UI profile macro is enabled in the shared ReadPico hardware configuration, including Nightly, and in the native simulator. Physical panel acceptance remains separate from simulator and build validation.

## Current implementation — 2026-09-30

Read Pico uses the SDK's **epdiy LCD_CAM + GDMA + RMT** backend for the
E0470A01 1216×684 raw parallel panel. The earlier Lovyan i80 experiment and
converted waveforms have been removed. `ReadPicoPower.cpp` retains the board's
rail timing and factory-PMU VCOM handling; neither calibration nor scan timing
was retuned in this review. Reader text keeps its existing four-tone
2-bit masks; AirPage and unfiltered custom sleep images use the native sixteen-tone
path described below.

SDK PR [#35](https://github.com/0x1abin/freeink-sdk/pull/35) is merged. This native
grayscale extension depends on SDK PR [#36](https://github.com/0x1abin/freeink-sdk/pull/36)
and pins its commit `fe12c72f9ca7dc72929c3203e204e700986f8d97`. Merge SDK #36 before
the paired CrossMux PR; a recursive checkout includes the required Read Pico drivers:

```bash
git submodule update --init --recursive
pio run -e readpico -e gh_release
python3 freeink-sdk/libs/display/EpdiyLcd/test/host/test_transactions.py
python3 scripts/tests/test_ui_font_fallback.py
```

### Desktop simulator

The `simulator_readpico` native environment runs the reader with a 1216×684 scan
framebuffer and 684×1216 portrait UI, native sixteen-level image output, the
SDK-aligned `{5,5,8,5}` portrait insets and Read Pico's 12/12/14 pt SD UI fonts.
Missing fonts and sizes fall back to the embedded faces. Mouse tap, hold and swipe
follow orientation; Up/Escape/Down stand in for the three capacitive keys. `P`
represents the PMU power key and is the only sleep wake input. `S` requests sleep.
The existing host battery and clock implementations are retained; PMU transport,
accelerometer behavior and physical key-strip coordinates are not simulated.

The window fits the usable desktop area without upscaling. Scheduled screenshots
are captured from the unscaled display pixels: 684×1216 in either portrait
orientation, 1216×684 in landscape. The 415,872-byte native image staging buffer
is static host-only storage; screenshots use one fallible, temporary ARGB buffer.
No hardware framebuffer or firmware allocation is added.

The existing EPUB image/AA renderer is retained. Native sixteen-level output
uses the current AirPage and custom sleep-image paths. Closing the EPUB toolbar
restores the firmware's B/W page snapshot, including its image geometry.

The PlatformIO and host-test dependencies pin simulator commit
[`33e585ff`](https://github.com/0x1abin/crosspoint-simulator/commit/33e585ff6452ea03f6a164f51e379f065e8c2e54).
A normal checkout needs no local dependency override.

Build/run with `pio run -e simulator_readpico -t run_simulator`. Keep test SD data
isolated with `CROSSPOINT_SIM_SD=/absolute/path/to/test-sd`; use the existing
`CROSSPOINT_SIM_INPUT_SCRIPT` and `CROSSPOINT_SIM_SCREENSHOTS` schedules. The fork's
`tests/run_readpico_self_test.sh <crossmux-root>` compiles the production host HAL
and checks large image offsets, gray transactions, native screenshots, scaled
touch and power-only wake/relaunch in all four orientations. On Linux without a
desktop, run it through `xvfb-run`.

Software validation completed on 2026-10-02: all five simulator builds, eight
host compatibility profiles, the four-orientation display/input self-check and
full `./bin/ci-check` (eight hardware builds, 807 host tests and SDK
transactions) passed with PlatformIO Core 6.2.0. The font/native-image tests
also passed against the independently fetched host-test dependency.

An isolated test SD covered English image pages, Chinese/mixed-script EPUB,
missing SD fonts/sizes, toolbar image restoration, four oriented screenshot
sizes, saved progress and a complete power-wake cycle. The current reader footer
can clip in landscape with the tested 12 pt SD UI face; the simulator reproduces
this existing layout. These checks do not accept that footer or the provisional
bottom clearance.

Simulator acceptance cannot close the physical bottom-clearance, waveform,
ghosting, power or sleep/wake acceptance items below.

### Nightly and Web release integration — 2026-10-01

Read Pico is included in the Nightly target table as `readpico`, using the
`readpico_nightly` environment and version `<version>-readpico-rc+<sha7>`. This
environment inherits the board profile and normal S3 Nightly logging; it does not
wait for a USB Serial connection at boot. Stable remains unsupported.

Packaging emits one ESP32-S3 binary set, a checksum file, and `global` / `zh-CN`
compatibility manifests referencing the same assets. Target, slug and board tag
are `readpico`; the model list also accepts the SDK runtime name `read_pico`.
The website displays **MindReset Read Pico**, at 684 × 1216,
and reuses the existing S3 download, full-install and inactive-slot OTA paths.
The option appears only after a regional Nightly index includes the target.
First installation replaces the factory partition layout; back up the full
16 MB Flash before proceeding. Later compatible installations use OTA.

This release-channel inclusion does not close the physical acceptance items
recorded below. Real-device reading, sleep/wake, browser installation and OTA
acceptance remain separate from build and package verification.

### Review fixes

- Combined B/W + gray presentation carries the requested refresh profile.
  Ordinary grayscale uses GL16; scheduled/manual FULL uses GC16 even when
  pixels are unchanged. Image pages enter this combined path before the ordinary
  image-base path, so they do not activate a second B/W waveform first.
- Failed rail bring-up skips drawing. A failed waveform leaves the baseline
  unknown; the next attempt clears physically and forces GC16. The highlevel
  copy paths advance `back_fb` only after a successful draw.
- PMU commands hold the existing recursive I²C lock across request, wait and
  response. Response bytes belong to the caller's fixed 44-byte stack buffer.
  This serializes RTC/VCOM requests without a second mutex allocation; the
  shared bus waits for the bounded PMU round trip too. Cached status fields use
  atomics because input polling reads them while command recovery updates them.
- Highlevel/base/selector allocation failures unwind owned buffers. Sleep
  releases the renderer tasks and buffers, allowing a later initialization.
  Line queues still own separately aligned 304-byte rows. Their pointer tables
  now use `sizeof(pointer)`: on the S3, two 64-entry tables request 512 bytes
  rather than 38,912 bytes, a **38,400-byte reduction in allocation requests**.
- UI keeps stable embedded font-slot IDs. Read Pico binds the slots to exact
  12/12/14 pt faces from the selected SD family when available, including Latin
  families; unload or a missing size falls back to embedded fonts. No retired
  font-ID map or allocation during font-ID lookup is needed. The built-in reader
  picker advertises only its actual 12 pt face; installed SD families expose
  their own sizes. Network and explicit BLE preparation do not immediately
  reload released fonts.
- Toolbar is the Read Pico first-boot default and respects saved settings.
  AirPage requests the board capability: Read Pico uses native sixteen-tone image output.

### Native sixteen-level images — 2026-10-01

`READ_PICO.grayscaleLevels = 16`; omitted legacy profiles remain at `4`. The
capability flows through FreeInkDisplay, HalDisplay and GfxRenderer. AirPage's
portrait QR parameters remain `w=684&h=1216`, now with `mode=gray16`.

AirPage BMP/JPEG and unfiltered custom sleep BMPs borrow the existing 415,872-byte
4bpp front framebuffer. Native brightness is `0` black / `15` white, even pixels
in low nibbles, with `(gray + 8) / 17` mapping and the usual orientation transform.
Ordinary commits use GL16; unknown baseline and fault recovery retain GC16. The
existing back buffer, 1bpp base and AA selectors are reused. No additional
full-screen allocation is introduced. BMP scratch is at most 10,240 bytes;
JPEG uses the existing fallible decoder and bounded row/MCU workspace. A B/W
proxy supports modal menus; closing them restores the original native image.

AirPage performs two consecutive full-screen white `FAST_REFRESH` calls before
displaying each new download and when the activity exits. These calls replace
the explicit FULL cleanup and consume any pending renderer refresh override.
They run before the native gray16 transaction, so cleanup cannot replace the
finished gray image with its B/W proxy. Legacy four-level output replaces its
existing white preclear with these two calls; the image refresh follows separately.
Identical downloads, history opens, popups and retry redraws retain their existing
refresh paths. Returning from the image to QR/settings/history no longer requests
an A4-only FULL refresh. Native grayscale waveforms and SDK fault recovery remain
unchanged. No additional framebuffer is allocated. The host recording HAL checks
refresh order/count and white pixels. FAST maps to differential DU on Read Pico,
so an unchanged second white frame may be skipped; verify residual gray tones on
the physical panel rather than assuming the two calls clean as thoroughly as FULL.

AirPage stores and displays historical originals. Set Cover preserves BMP bytes;
JPEG uses the existing Gray8 BMP writer selected by an explicit output enum.
Short writes fail and the `.part/.bak` installation transaction remains in place.
Native sleep decode/refresh failure displays the default screen. Text AA, book
covers, alpha sleep overlays and all existing filters retain their prior paths.

The initial local checkout, including separate font work, passed `readpico`,
599 host checks, SDK facade tests and the complete SDK transaction harness on
H2O Linux (diagnostics on/off). Formatting and cppcheck also passed. The broader
hardware build was stopped at the user's request; its earlier X4 Pro attempt
failed on undefined `USBMSC` / `USB` in `UsbMassStorage.cpp`, so full-platform
validation is not recorded as passing.

The isolated grayscale-only PR candidate passed formatting and the `readpico`
build. Its macOS host run passed 598/599 checks; the sole `UiFontFallback` failure
is Clang's unused-constant warning, reproduced on unchanged main. No font changes
are included in this PR. NativeGrayscale additionally passed without any
`.pio/libdeps` cache, using the firmware's existing pinned JPEGDEC source through
CMake. The isolated firmware was built but not flashed again.

The connected ESP32-S3 Read Pico was backed up and flashed **only at app0
`0x10000`**; the live partition layout and boot selection were checked first.
The uploader verified the written image. The 5,904,768-byte firmware SHA-256 is
`e01c746963db6df1053cff8eb3018d7c6a8f90da9ef3c24010677d77736f0cba`.
Backups, firmware, logs and BMP/JPEG staircase fixtures are retained locally in
`.pio/readpico-gray16-20261001/`. Boot/NVS/OTA metadata and SD content were not
written. The existing font work is included in this local firmware.

The serial capture started after early initialization. At 14/24/34 seconds it
recorded 75,679 bytes free internal heap, a 69,559-byte minimum and a 31,732-byte
largest block. Opening AirPage completed Wi-Fi and MQTT connection; during Wi-Fi
setup free heap was 43,815 bytes, minimum 42,259, largest block 31,732. No panic
or OOM was observed in this captured window. Four further 684×1216 JPEG downloads
and decodes completed (1,490–1,636 ms decoding); stable AirPage free heap was
42,791 bytes, minimum 33,167, largest block 31,732. This measures decoding, not
panel refresh latency. A later cover save completed JPEG-to-8-bit-BMP conversion;
closing Confirmation decoded the native JPEG again. Sleep loaded the resulting
684×1216 `/sleep.bmp` and entered deep sleep, disconnecting USB. Minimum internal
free heap reached 33,151 bytes. These traces confirm the software flow, not the
appearance of the image. The user confirmed normal screen and touch behavior and
the `gray16` QR mode. After wake, serial reconnected and idle heap returned to
75,679 bytes (minimum 69,559, largest block 31,732); early wake initialization
was not captured.

Initial physical acceptance **passed by user observation**: sixteen tones are
distinguishable with the expected black/white polarity; the saved unfiltered
sleep cover, modal dismissal and wake behave normally. This is a visual report,
not instrumented optical measurement. Separate BMP/JPEG comparison across all
four orientations, at least three recorded sleep/wake cycles and long-term
ghosting remain unverified.

### Current footer fonts and INX home geometry — 2026-10-01

Read Pico's reader footer uses independent stable font slots: chapter title,
page counters and reading percentage use **8 pt**, and the estimate marker `~`
uses **10 pt**. Measurement, truncation and drawing use those same slots. The
selected SD family supplies exact sizes when available; a missing or failed face
uses the corresponding embedded font and Chinese fallback. Existing instances
are reused across slots, and font switching/unload clears their bindings. The
manager reserves five instances on Read Pico (reader plus 8/10/12/14 pt), avoiding
vector growth during loading; other targets retain four. Other UI slots remain
12/12/14 pt on Read Pico, and battery percentage remains embedded 8 pt.

The current board profile is `viewableInsets = {5, 5, 8, 5}` in portrait
(top/right/bottom/left, pixels). The 8 px physical edge rotates with the existing
orientation transform: portrait bottom, clockwise landscape left, inverted
portrait top, counterclockwise landscape right. These are layout insets, not
measured bezel dimensions. Reader text and existing safe-area consumers adopt
them; the reader progress bar retains its fill-to-screen-edge behavior.

INX home derives its tabs, five book layouts, empty state and battery from
`UITheme::getInstance().getScreenSafeArea(renderer, false, false)`, with theme
padding inside that area. All five INX main pages (recent, files, apps, settings and statistics) share
header geometry and integer tab boundaries for drawing and touch. Home
content is clipped to one shared rectangle. The internal footer reserve is the
larger of 40 px and button-hint height, deducted once. Read Pico's battery text
starts **24 px above the safe bottom**; other targets retain 30 px. The 15×12 px
icon starts 6 px below the text and keeps the 12 px internal right offset. In
portrait this gives 14 px below the icon and 17 px to its right. Home font sizes,
weights and line spacing are unchanged. The four other main pages reserve
content below the same header, while retaining their bottom layouts. Apps use
the same content rectangle for grid drawing and hit testing; non-INX headers
and embedded pickers retain their existing geometry.

#### Historical local UI firmware tests

These snapshots used SDK `2d40f2bafe143999988eecd8374ce0b8c1a36a59` plus local
native-grayscale changes. They are historical device evidence, not the final
isolated PR candidate. Firmware, hashes and test/build/flash/boot logs remain in
the ignored directories below. Each upload checked the device/partitions/active
slot, wrote app0 only at `0x10000`, verified the written digest and preserved data
partitions and boot selection. Only the first snapshot made a backup.

| Snapshot directory under `.pio/` | Layout | Firmware SHA-256 |
| --- | --- | --- |
| `readpico-footer-20261001` | Footer 8/10 pt; previous insets | `7ee5773a8a0dd47b3510e1653f0f617c968e93babadd3a40895848d94d7d5020` |
| `inx-safe-area-20261001` | INX safe area; uniform 5 px | `16b217b1bc01b2f01d281378320c3d1a12a676a6fdd9bc96bc4b2cd0b5868698` |
| `readpico-bottom8-20261001` | Bottom 8 px; battery offset 30 px | `fe498001ae1b647a755298712b0c61f7427748fb4d3752f14e3f74f8403abb50` |
| `readpico-home-battery6-20261001` | Bottom 8 px; battery offset 24 px | `6dbae4ebe0ff9635098bb73763167cb355eafd74b732e28278c81fe4347695a2` |

Those snapshots passed the font/layout checks and `readpico` / `gh_release`
builds recorded beside each image. Their 40-second boot captures confirmed
Read Pico, SDMMC and display initialization, with no observed panic/OOM.
The last image was 5,904,928 bytes; idle internal free/minimum/largest block
was 75,459 / 69,003 / 31,732 bytes at 10/20/30 seconds. Five `GFX Outside range`
warnings occurred during Boot before InxRecent; similar warnings occurred in
the first footer snapshot, but not in the two intermediate captures. Their cause
is undiagnosed. Physical acceptance remains pending: verify long Chinese chapter
and book titles, footer/percentage/estimate readability, battery clearance,
all five home layouts, edge taps and page turns in all four orientations.

The isolated PR candidate starts from the latest CrossMux and SDK main branches;
it includes only the font and layout changes above. Its exact revisions, build
hashes and check results are recorded separately under `.pio/readpico-pr-review/`
and in the paired PR descriptions. It is not flashed again by this PR task;
historical startup logs do not establish final-candidate physical acceptance.

### Validation record

SDK rebased onto `ab8c859389725bc91a0d44c2de32c24c64fc893b`; CrossMux onto
`64282343da004f434edf1f4ccb0a6170888e523f`. Both original histories are saved
locally as `codex/backup-readpico-before-rebase` in their respective repositories.
SDK's final rebase tree equals the original port plus main's eight changed paths.
CrossMux range-diff preserves every nonempty source commit; SDK-only pointer
bumps become empty because the official dependency stays at main. The only
additional rebase-stage commit fixes the font test harness's resolver interface.
Both original PR branches were updated with leases naming their saved old SHAs.

The rebase combination passed `readpico` and `gh_release` builds, 59 relevant
font/image tests, and the SDK SSD1677/combined-AA checks. The review candidate's
host transaction checks compile the complete driver, wrapper, highlevel and PMU
source. They cover mode selection, unchanged FULL, rail/draw failure recovery,
seven injected allocation failures, sleep/reinitialization, queue allocation size
and simultaneous RTC/VCOM/input polling, including stale-session recovery.
Full `./bin/ci-check` passed format, static analysis, all seven existing hardware
environments and 597 host tests. The official `ba3c44d7` SDK separately passed
the shared `gh_release` build and cppcheck. Combination builds and exact final
candidate revisions are recorded in the PR descriptions. These are software
checks, not electrical or optical acceptance.

Physical acceptance remains pending: record the exact firmware/SDK revisions,
boot and heap logs, text/image turns across periodic and manual cleanups,
grayscale-to-menu transitions, touch/strip/power gestures, SD read/write,
RTC synchronization during refresh, battery/USB readings, shutdown and at least
three sleep/wake cycles. Confirm saved menu/font choices survive reboot.

## Second convergence review — 2026-09-30

The LCD, renderer and board initialization interfaces in the local vendored
library now return errors to `epdiyLcdBegin()`. IRQ/GDMA/buffer, semaphore and
worker-creation failures release only resources actually created. Teardown is
idempotent and never deletes a null task handle. The generic SDK `PanelDriver`
interface is unchanged. The native checks compile the complete production LCD,
renderer, line queue and clear code; raw register operations are modeled, while
resource APIs record ownership and inject failures. They do not prove electrical
behavior or ISR timing on a device.

Synchronous clear runs with feed tasks idle, borrows an existing feed buffer,
and constructs its mask directly. It no longer allocates scratch buffers per
phase. Clear phase counts, panel timings and waveform data remain unchanged.
Grayscale composition writes both pixels of each byte together; all 64 paired
base/LSB/MSB combinations are checked across byte and row boundaries. Reduced
framebuffer read/modify/write traffic is a mechanism, not a measured speed gain.

The accelerometer-only gesture path polls at 80 ms to match the configured
SC7A20H 12.5 Hz rate. Wake, reader entry, orientation/mode changes and failed
reads invalidate the derivative baseline. The first valid sample establishes a
baseline without triggering a page turn. Gyroscope targets retain 50 ms polling.
Host checks cover both paths; existing gesture thresholds still need hardware
calibration.

### Historical 24 px bottom clearance

The frozen 2026-09-30 firmware used portrait `viewableInsets = {9, 3, 24, 3}`
(top/right/bottom/left, in pixels). The observations below belong to that older
configuration. Its provisional 24 px clearance was not a measured bezel dimension;
the current configuration is described above.

Reader text/status bars and FreeInkUI screens already consume the renderer's
oriented insets. The legacy UITheme safe-area and list-capacity calculations now
use the same area on Read Pico. FreeInkUI drawing and touch routing share the
resulting layout; the raw touch calibration is unchanged. Verify the lowest row,
progress bar and bottom controls in all four rotations, including taps near the
new boundary, before accepting the clearance. The user confirmed the recently
read screen's battery and bottom controls are fully visible after the first
upload. Reader status text drops the old additional 4 px upward offset, placing
its battery, clock and page counters 4 px closer to the safe bottom edge; the
recently read screen keeps its accepted geometry. The user reported obstruction
after this reader adjustment. That firmware was frozen for merging with
reader-footer obstruction recorded as an outstanding issue.

The frozen firmware code at CrossMux `f63170ac` with SDK `4af3673` passed the
full pre-integration `./bin/ci-check`: formatting, static analysis, the seven
existing hardware builds and all 598 host tests. SDK resource/transaction checks
and the FreeInkUI host suite also passed. The normal Read Pico image was built
and flashed; the optional diagnostics configuration compiled separately before
the final footer adjustment. This validation does not accept the known optical
issue or the remaining physical checks.

### Optional measurements

`FREEINK_READPICO_DIAGNOSTICS=1` enables two fixed 12-byte statistics records and
bounded stack timing guards, with no heap allocation. It is absent from normal
builds. PMU logs report command count, last/max lock wait and last/max command
hold duration. Display logs report conversion time, its maximum and the existing
diff/scan/copy timings. PMU statistics update under the existing I²C lock;
frame statistics use the serialized display path. Diagnostic logging itself adds
latency, so compare ordinary builds too. `ENABLE_SERIAL_LOG` is required for PMU
console output.

For a temporary diagnostic build, copy `platformio.ini` to a temporary config,
append this environment and pass that file to `pio run -c <temporary-config>
-e readpico_diagnostics` from the repository root:

```ini
[env:readpico_diagnostics]
extends = env:readpico
build_flags =
  ${env:readpico.build_flags}
  -DFREEINK_READPICO_DIAGNOSTICS=1
```

The PMU transaction keeps its existing bus lock; separate it only after measured
touch latency justifies the additional lock. Collect latency during RTC/VCOM
retries, heap/largest-block watermarks, reading turns and sleep/wake before tuning
queues or memory reserves.

### Initial device check

At the user's request, CrossMux `c4b6cee2` with SDK `4af3673` was uploaded through
USB Serial/JTAG to the connected ESP32-S3 revision 0.2 (16 MB Flash, 8 MB PSRAM).
`pio run -e readpico -t upload --upload-port <verified-readpico-port>` passed,
including the uploader's written-data digest verification. This is the normal
`readpico` build; the optional diagnostic build was compiled separately.
The uploaded `firmware.bin` SHA-256 is
`fa23c6feac5bbc8ba145a453500393e449261b5481819cbda7e5de19a3c5cd68`.

The captured boot and first 30 seconds of operation show the board, PMU,
SC7A20H, external RTC, SDMMC and panel initializing successfully. Menu navigation
appears in the input/activity logs without panic or OOM. Immediately after panel
initialization, reported free PSRAM was 6,283,572 bytes and free internal heap
112,763 bytes. At 20 seconds, internal free/minimum/largest block were
77,867 / 67,979 / 31,732 bytes. These are one-session observations, not a leak,
latency or long-term stability test.

The existing serial screenshot command returned only part of the 103,968-byte
buffer; no screenshot was accepted. The bottom 24 px clearance has the user's
confirmation on the recently read screen; rotated controls remain pending.
Reading refresh modes, calibrated tilt, PMU contention, battery operation and
three sleep/wake cycles remain pending. Local evidence is kept under
`.pio/readpico-flash-round2/` (ignored, not a repository dependency).

After the user reported excessive space beneath the reader footer, the 4 px
status-text adjustment was uploaded as CrossMux `f63170ac` with the same SDK.
Written-data digest verification passed again; the image SHA-256 is
`f133b643d7b5c65edb8b887d6e9ebeb55066f48539a6260aec9012f4f759e3ee`.
Its recorded normal boot shows the same successful peripheral initialization and
no observed panic/OOM. Internal free heap was 83,771 bytes at both 10 and 20
seconds (minimum 77,211, largest block 36,852). The second image and runtime log
are retained under `.pio/readpico-flash-footer/`. Reader-footer visual acceptance
failed: the user reported obstruction after the 4 px adjustment. No additional
Flash backup was performed for this upload, and no firmware or further upload is
planned as part of the frozen-version merge.

### CI dependency follow-up

SDK #35 includes a standalone Read Pico host-check workflow. CrossMux runs
`python3 freeink-sdk/libs/display/EpdiyLcd/test/host/test_transactions.py` in its
existing host-test CI step and local `bin/ci-check`. Local hardware checks build
`readpico`; Hardware CI builds and verifies the `readpico_nightly` package. The
official merged SDK gitlink supplies these drivers without a temporary PR pin or
conditional skip. The existing accelerometer/gyroscope check remains in the host
suite.

## Historical port investigation

The dated notes below preserve the original hardware evidence and experiments.
Earlier build flags, Lovyan interfaces, refs, blockers and validation statements
are historical; the current implementation and commands above take precedence.


Read Pico is an ESP32-S3 e-paper dev board from Shenzhen MindReset Technology
Co., Ltd. Its 4.7" panel (E0470A01) has **no on-glass controller**: the MCU clocks
every gate line through the ESP32-S3 LCD (i80) peripheral, and a SY7636A PMIC
makes the waveform rails under FCA9555 expander control. That is the same display
class as the LilyGo T5 S3 and M5Stack PaperS3, so the port rides the SDK's
existing `LgfxEpdDriver` / `LovyanGFX Panel_EPD` path rather than any SPI
controller driver.

This document is both the **hardware contract** and the **frozen interface
specification** for the port. Three parallel implementation tasks consume the
symbol names in [§3](#3-frozen-interface-specification) verbatim. Nothing in §3
may be renamed without editing this file first.

| Item | Value |
|---|---|
| CrossMux worktree revision | `80c67543c3be1689e77e4b85c78abd9e5a4fff7b` (`80c67543`), clean at time of writing |
| Pinned SDK gitlink | `094976e1d47ad7120cf461fec5f6b737eaabf13f` (`freeink-sdk`, `heads/main`) |
| Reference firmware | [`MindReset/read_pico_firmware`](https://github.com/MindReset/read_pico_firmware) `main`, Apache-2.0, ESP-IDF v6.1 |
| Official docs | [`MindReset/dot_web_docs`](https://github.com/MindReset/dot_web_docs) `zh-Hans-CN/read_0/{index,start,firmware}.mdx` → <https://dot.mindreset.tech/docs/read_0> |
| Panel vendor table source | `components/e0470_epaper_waveform/README.md` (Apache-2.0, ships with the board) |
| Build/verification status | **No build, no CI, no flash, no hardware check was run** — see [§4](#4-verification-status) |

Planned commands (not executed in this round):

```sh
pio project config --lint                 # env resolution only (PIO 6.1.19 has no `-e`)
pio run -e readpico                       # PASSED 2026-09-25 — Flash 92.3% (6,046,171/6,553,600)
pio run -e default                        # PASSED 2026-09-25 — shared X3/X4 C3 image, Flash 97.8%
pio run -e metalio_eink4 -e waveshare_epaper_397   # PENDING shared-header regression
pio device monitor --port <verified-readpico-port> --baud 115200
```

## 1. Hardware contract

### 1.1 Summary

Status key: **V** = verified from cited source, **A** = assumed (needs confirming),
**U** = unknown / to confirm on hardware.

| Area | Fact | Status | Evidence |
|---|---|---|---|
| Platform | ESP32-S3 (Xtensa dual-core LX7), 512 KB SRAM | V | `dot_web_docs/zh-Hans-CN/read_0/index.mdx` spec table |
| Platform | 16 MB flash + 8 MB **octal** PSRAM, both configured at **120 MHz** (experimental) | V | `read_pico_firmware/README.md` (Hardware); `sdkconfig.defaults` |
| Platform | 120 MHz timing depends on the installed flash part; IDF temperature compensation is deliberately **disabled** (Zbit ZB25VQ128, vendor `0x5E`, is unverified by IDF, enabling it aborts in `do_system_init_fn`); `sdkconfig.ci` uses default timing for compile-only checks | V | `sdkconfig.defaults` comment block; `read_pico_firmware/README.md` (Build & Flash); `components/read_pico/README.md` (Zbit `0x5E`, HPM via `read_pico_flash_hpm.c`) |
| Platform | Partition table: `nvs` 0x9000/20 K, `phy_init` 0xE000/4 K, `factory` 0x10000/**2 MB**, `spiffs` 5 MB | V | `partitions_16M.csv` |
| Display | Panel E0470A01, 4.7", monochrome, **16 native gray levels** | V | `components/e0470_epaper_waveform/{include/e0470_epaper_waveform.h,README.md}`; `dot_web_docs/.../index.mdx` |
| Display | Native resolution **684 × 1216** (portrait face); see §1.2 for the README's "1216 × 684" | V | see §1.2 |
| Display | 16-bit parallel data bus D0–D15 = GPIO 4–18, 45; control XLE/XSTL/XCL/SPV/CKV = GPIO 3/46/21/47/48 | V | `read_pico_firmware/README.md` (Pinout); `components/read_pico/read_pico_board.c` (`D0`…`D15`, `EPD_XLE/XSTL/XCL/SPV/CKV`) |
| Display | No on-glass controller; MCU clocks every row via the LCD peripheral | V | `read_pico_firmware/README.md` (Hardware, Display row); `components/epdiy/src/output_lcd/*` |
| Display | Pixel clock used at run time: **18 MHz** (board range 12–24 MHz; vendor default 12 MHz) | V | `components/read_pico/read_pico_init.c` (`READ_PICO_PCLK_MHZ 18`); `read_pico_epd_timing.h` (`READ_PICO_EPD_PCLK_MIN_MHZ 12` / `_MAX_MHZ 24`) |
| Display | Panel VCOM is factory-calibrated and lives in the PMU; no local copy, no user entry point | V | `read_pico_firmware/README.md`; `read_pico_pmu/README.md`; `components/read_pico_pmu/include/read_pico_pmu.h` (`vcom_get` 500..2500 mV, multiple of 10, `ESP_ERR_NOT_FOUND` otherwise) |
| Display power | SY7636A PMIC at I²C `0x62`; EN + VCOM_EN + PGOOD routed through the FCA9555; VCOM set to 1290 mV by the reference firmware | V | `components/sy7636a/include/sy7636a.h`; `components/read_pico/read_pico_board.c` (`sy.power.vcom_mv = 1290`) |
| Input | CST836U self-capacitive touch, **two points**, I²C `0x15`, INT# = GPIO43 (open-drain, active-low, 10 k pull-up **on the FPC**), reset via FCA9555 P0.7 | V | `read_pico_firmware/README.md`; `components/cst836u/include/cst836u.h`; `components/read_pico/include/read_pico_board.h` |
| Input | Touch panel is **taller than the display**: three capacitive key zones sit in the strip below the image, undrawn, hit-tested by raw coordinate (centres x = 80/240/400, y ≈ 1500, pitch 160; split at y = 1300; display y max = 1216) | V | `main/ui/ui_menu.h` (`UI_KEY_AREA_TOP 1300`, `UI_KEY_PITCH 160`, `UI_KEY_1..3`); `main/app/app_loop.c` (hit-test on raw touch coords) |
| Input | Power key is owned by the CW32L010 PMU (not an ESP GPIO); events DOWN/UP/SHORT/LONG/FORCE_OFF over I²C | V | `main/apps/app_key.c`; `components/read_pico_pmu/include/read_pico_pmu_protocol.h` (`PMU_EVT_KEY_*`) |
| Input | After the touch chip enters deep sleep it stops ACKing I²C; **only the RST pulse brings it back** | V | `components/read_pico/include/read_pico_board.h` (`read_pico_touch_reset`); `components/cst836u/cst836u.c`; `components/cst836u/include/cst836u.h` |
| Storage / bus | TF card over **1-bit SDMMC**: CLK 38 / CMD 42 / D0 44, internal pull-ups, 40 MHz (`SDMMC_FREQ_HIGHSPEED`); no SD pin for card detect | V | `components/read_pico/read_pico_sd.c`; `read_pico_firmware/README.md` (Pinout) |
| Storage / bus | Card detect on FCA9555 **P0.6**, active-low (`0` = card present); a failed read must not be read as "present" | V | `components/read_pico/read_pico_board.c` (`read_pico_sd_present`); `main/apps/app_ioe.c` |
| Storage / bus | Shared I²C: SCL 40 / SDA 39 at 400 kHz | V | `read_pico_firmware/README.md` (Pinout); `components/read_pico/read_pico_board.c` (`board_init`) |
| Power | CW32L010 PMU at I²C `0x2A` owns battery, host enable rail, power key, indicator LED, RTC and alarm; length-prefixed CRC frames, not a register map | V | `components/read_pico_pmu/README.md`; `components/read_pico_pmu/include/read_pico_pmu.h`; `.../read_pico_pmu_protocol.h` |
| Power | FCA9555 INT# on GPIO41 is **not an RTC-capable pin** → light-sleep wake only | V | `components/read_pico/include/read_pico_board.h` comment; `main/sleep.c` (`gpio_wakeup_enable` + `esp_sleep_enable_gpio_wakeup`) |
| Power | Referenced "deep sleep" and "power off" both drop the host EN rail **through the PMU** — there is no `esp_deep_sleep_start()` path and no `ext0`/`ext1` wake source | V | `main/sleep.c` (`app_enter_host_sleep`); `main/apps/app_sleep.c`; `read_pico_pmu/README.md` (`HOST_SOFT_SLEEP`, `REQUEST_OFF`) |
| Optional | Buzzer on GPIO2 through an AO3400A NMOS (active-high, idle duty 0), LEDC timer0/channel0, 10-bit, carrier 80 MHz/1024 → 78 kHz nominal | V | `components/read_pico/read_pico_buzzer.c`; `read_pico_firmware/README.md` (Pinout) |
| Optional | SC7A20H accelerometer, I²C `0x19`, INT1 = GPIO1, `WHO_AM_I` = `0x11`, version = `0x28`; tap / orientation / free-fall / FIFO + pickup-to-wake; accelerometer only (no gyroscope) | V | `components/sc7a20h/include/sc7a20h.h`; `components/read_pico/read_pico_init.c` |
| Optional | Accelerometer at rest on the bench: chip frame X = −5, Y = −119, Z = −1005 mg; device frame is about +Z face-up; the board header carries the negated zero offsets 119/5/1005 mg | V | `components/read_pico/include/read_pico_board.h`; `components/read_pico/read_pico_init.c` (`read_pico_accel_to_device`: Xd = −Yc, Yd = −Xc, Zd = −Zc) |
| Optional | Battery: user-replaceable 2050 mAh LCO pack; charging, indicator LED and charge state are PMU-reported | V | `dot_web_docs/zh-Hans-CN/read_0/index.mdx`; `components/read_pico_pmu/README.md` |
| Optional | Wi-Fi 802.11b/g/n, BLE 5.0 @ 2 Mbps PHY (S3 silicon) | V | `dot_web_docs/zh-Hans-CN/read_0/index.mdx` |
| Optional | USB-C, native USB; reference console is USB Serial/JTAG | V | `sdkconfig.defaults` (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`); `read_pico_firmware/README.md` (Build & Flash) |
| Optional | Frontlight, microphone, haptics, speaker/codec, LED strip, environmental sensor | U | Not present in any fetched source; treat as absent until a schematic says otherwise |

### 1.2 The 1216 × 684 versus 684 × 1216 question

Both numbers appear upstream. They are the same rectangle written in two frames,
and the evidence separates them cleanly:

| Source | Writes | Meaning |
|---|---|---|
| `read_pico_firmware/README.md` (Hardware → Display) | "1216 × 684" | The epdiy **scan/framebuffer** frame: 1216 source columns × 684 gate lines |
| `components/epdiy/src/displays.c` (`E0470_DISPLAY`) | `.width = 1216, .height = 684, .bus_width = 16, .bus_speed = 24` with the comment `// E0470A01 / 684×1216 40pin` | Same: `width` is "Width of the display in pixels" = pixels per row (`components/epdiy/src/epd_display.h`) |
| `components/e0470_epaper_waveform/include/e0470_epaper_waveform.h` and `/README.md` | "E0470A01（684×1216，40pin）" | The **portrait glass face** (684 wide × 1216 tall) |
| `components/read_pico/read_pico_init.c` | `ESP_LOGI(TAG, "Initializing 684x1216 EPD")` then `epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT)` | Portrait is the user-facing orientation |
| `main/ui/ui_menu.h` | "Display y max is 1216", key strip at y ≈ 1500, key centres x = 80/240/400 | Independent confirmation that the **portrait** frame is 684 wide × 1216 tall and that the touch plane extends past it |

**Conclusion.** The E0470A01 is a **684 × 1216** panel. "1216 × 684" in the
official README is the same panel in epdiy's landscape scan order and matches
`E0470_DISPLAY` exactly; "684 × 1216" is the portrait glass and is what the app
presents after `EPD_ROT_INVERTED_PORTRAIT`. There is no contradiction to resolve.

**How the SDK profile records it.** `BoardProfile.displayWidth` /
`displayHeight` are the raw-parallel **scan** frame, exactly like LilyGo T5 S3
(960 × 540 = columns × rows):

```cpp
displayWidth  = 1216;   // source columns per line  -> Panel_EPD panel_width / memory_width
displayHeight = 684;    // gate lines               -> Panel_EPD panel_height / memory_height
```

Evidence for that mapping: `LgfxEpdDriver::setup()` assigns
`pc.memory_width = pc.panel_width = w` and `pc.memory_height = pc.panel_height = h`
from `BoardConfig::ACTIVE.displayWidth/Height`
(`freeink-sdk/libs/display/FreeInkDisplay/src/driver/LgfxEpdDriver.cpp:92-93`,
`:224`), and `LgfxEpdDriver::geometry()` derives the framebuffer as
`(w / 8) * h` (`:213-218`). The resulting 1-bpp framebuffer is
**1216 / 8 × 684 = 103,968 bytes ≈ 104 KB** — hence `FREEINK_FB_PSRAM` (§3.1).

### 1.3 EPD timing parameters (`read_pico_epd_timing.h`)

The header solves line/frame parameters from the pixel clock; it does not carry a
fixed table.

| Parameter | Value / rule | Evidence |
|---|---|---|
| Pixel clock range (board support) | `READ_PICO_EPD_PCLK_MIN_MHZ 12`, `READ_PICO_EPD_PCLK_MAX_MHZ 24`; the cap is DMA feed capability, **not** a panel spec | `read_pico_epd_timing.h` |
| Pixel clock in use | 18 MHz (`read_pico_init.c`); `bus_speed * 8 / bus_width` = 24 × 8 / 16 = 12 MHz is the vendor default the firmware overrides | `read_pico_init.c`; `components/epdiy/src/displays.c` |
| Pixels per clock | `p = bus width / 2 bits-per-pixel` → 16 / 2 = 8 | `read_pico_epd_timing.h` (`px_per_clk` comment) |
| Data length | `L_DL = H / p` = 1216 / 8 = 152 clocks | same |
| Line total | `H_all = L_SL + L_BL + L_DL + L_EL`; `T_line = H_all / f` (µs, f in MHz) | same |
| Frame total | `T_frame = T_line · V_all`, `V_all = F_SL + F_BL + V + F_EL` | same |
| Frame rate | `ν = 10^6 / T_frame` (stored as `frame_hz_x100`) | same |
| Refresh wall time | `T_refresh = N · T_frame` for an N-phase waveform (`read_pico_epd_refresh_ms`) | same |
| Blanking floors | `T(L_SL) ≥ 0.3 µs`, `T(L_BL) ≥ 0.8 µs`, `L_EL ≥ 4 clocks`; extra line time goes to `L_EL`, **never** `L_BL` | same |
| Source-validity rule | Source voltages are valid only during `L_DL`, so CKV must cover that window; vendor `LGONL ≈ 13.4 µs` (default CKV-high cap 134 = 13.4 µs, `ckv_high_max_01us`) | same |
| FULL-profile frame target | Default = the waveform header setpoint `E0470_WAVEFORM_FRAME_US` 11 090 µs (ν ≈ 90 Hz, the panel algorithm document's §8 design point); live-tunable; floor ≈ 700 × shortest line | `read_pico_epd_timing.h`; `e0470_epaper_waveform.h` |
| Scan profiles | `READ_PICO_EPD_SCAN_FULL` (pad the line to the waveform frame period) and `READ_PICO_EPD_SCAN_FAST` (shortest legal line). Bench GC16 looks the same from 7–18 ms, so neither is used as a drive knob | `read_pico_epd_timing.h` |
| Re-solve rule | Changing the pixel clock requires re-solving blanking **and** CKV; calling the frequency setter alone leaves the previous clock counts and CKV fills the whole line at high f | same |
| Profile switch cost | "a few registers plus one RMT rebuild", between two refreshes only | same |

`T_frame` for the pinned 18 MHz / FULL profile is solved at run time by
`read_pico_epd_scan()`; the header does **not** contain the resolved integer
values, so none are reproduced here. No refresh time was measured in this round.

### 1.4 EPD power chain

`components/read_pico/read_pico_board.c` is the authoritative sequence.

**FCA9555 Port-0 pin table** (bits are `1U << n` in the same file, and the app
page `main/apps/app_ioe.c` names them):

| Bit | Name | Dir | Role | Evidence |
|---|---|---|---|---|
| P0.0 | `MODE` | OUT | EPD MODE pin. Init sets it HIGH; `board_poweron`/`board_poweroff` both leave it HIGH | `IOE_MODE (1U << 0)`; `board_init`, `board_poweron`, `board_poweroff`; `main/apps/app_ioe.c` (`IOE_BIT_MODE 0`) |
| P0.1 | `XOE` | OUT | EPD output enable. Driven **LOW while powering up**, raised only after the rails are ready; driven LOW again before power-down | `IOE_XOE (1U << 1)`; `board_poweron`, `board_poweroff` |
| P0.2 | `CW_INT` | **IN** | CW32L010 PMU interrupt; routed through the expander, and the light-sleep wake source | `IOE_CW_INT (1U << 2)`; `IOE_CONFIG_PORT0 0x64`; `read_pico_pmu/README.md` |
| P0.3 | `SY_EN` | OUT | SY7636A enable. LOW resets every PMIC register; I²C is live only while HIGH | `IOE_SY_EN (1U << 3)`; `components/sy7636a/include/sy7636a.h` |
| P0.4 | `SY_VCOM_EN` | OUT | External VCOM enable (raised last by `sy7636a_power_on`) | `IOE_SY_VCOM_EN (1U << 4)` |
| P0.5 | `SY_PGOOD` | **IN** | SY7636A power good | `IOE_SY_PGOOD (1U << 5)`; `main/apps/app_power.c` (`PWR_PGOOD_BIT (1U << 5)`) |
| P0.6 | `SD_CD` | **IN** | TF card detect, **active-low** (`0` = present) | `IOE_SD_CD (1U << 6)`; `read_pico_sd_present()` |
| P0.7 | `TP_RST` | OUT | CST836U **active-low** reset | `IOE_TP_RST (1U << 7)`; `read_pico_tp_rst()` / `read_pico_touch_reset()` |

`IOE_CONFIG_PORT0 = 0x64` = bits 2, 5, 6 as inputs and everything else as output
(`main/apps/app_ioe.c` shows `IOE_CFG0_EXPECT 0x64`). Port 1 is unused
(`CFG1 = 0xFF`). The reference app deliberately freezes direction and inversion:
`app_ioe.c`'s header comment says CFG/INV are read-only because changing a
direction can drive a sense pin (PGOOD, card detect), and `fca9555.h` repeats the
warning. Expander: FCA9555 at 7-bit **0x24** (`FCA9555_ADDR_DEFAULT`, A2=1 A1=0
A0=0), 400 kHz, INT# open-drain and **not latched** — reading Input releases it
(`fca9555.h`; `read_pico_board.c` `fca9555_selftest`).

**Power-up** (`read_pico_board.c` `board_poweron`), verified order:

1. Route `EPD_XSTL` (GPIO46) to the LCD DE signal
   (`esp_rom_gpio_connect_out_signal(EPD_XSTL, LCD_H_ENABLE_IDX, false, false)`).
2. `MODE = 1`, `XOE = 0` (both through one expander write).
3. `sy7636a_power_on()`: write VCOM / VLDO / delays → set `ON_OFF` → wait PGOOD →
   raise `VCOM_EN`.
4. `XOE = 1`, `rails_on = true`.

**Power-down** (`board_poweroff`): `XOE = 0`, `MODE = 1`, 1 ms delay,
`sy7636a_power_off()`. It does **not** clear `VCOMCTL`; the app page treats
VCOM_EN as read-only (`main/apps/app_power.c`).

**Touch reset timing** (`board_init` and `read_pico_touch_reset`): drive P0.7 LOW,
wait **10 ms**, release, then wait **50 ms** for chip boot. `cst836u.c` names the
same numbers (`RST_HOLD_MS 10`, `RST_BOOT_MS 50`).

**VCOM rule.** The panel VCOM is calibrated at the factory and bound to *that*
unit's panel inside the PMU. `read_pico_pmu_vcom_get()` is read-only and returns
`500..2500 mV` in 10 mV steps, or `ESP_ERR_NOT_FOUND` when unset / old firmware /
read failure; `read_pico_pmu_vcom_set()` exists only for the factory page. It is
**not** stored locally and has **no user entry point**
(`read_pico_firmware/README.md`; `read_pico_pmu/README.md`;
`components/read_pico_pmu/include/read_pico_pmu.h`).

> **Conflict to carry forward (V).** The README says the firmware "reads it once
> at boot to configure the driver", but the reference firmware's driver path does
> not: `read_pico_init.c` hardcodes `READ_PICO_VCOM_MV 1290` and passes that to
> `epd_set_vcom()`, and `read_pico_board.c` uses the same 1290 mV for the PMIC.
> The PMU getter is consumed by the power/factory pages only.

> **HARDWARE SAFETY RULE — no VCOM fallback (2026-09-25).** The board vendor's
> own handover states that the panel voltage parameter was written into the PMU at
> the factory and **must not be changed, or the hardware is permanently damaged**.
> The port therefore:
>
> - **never writes** VCOM anywhere except the SY7636A when bringing the panel up;
>   it never writes the PMU's stored value (`read_pico_pmu_vcom_set()` exists only
>   for the factory page and is not called),
> - **has no fallback constant.** `BoardReadPico::pmuVcomMv()` returns `-1` when
>   the factory value cannot be read or fails the `500..2500 mV / multiple of 10`
>   range check, and `epdPowerOn()` treats `-1` as *"do not power the panel"* —
>   it returns before energising any rail.
>
> Rationale: driving the glass at a VCOM that does not match it forces a DC
> imbalance and destroys the panel, so a guessed value is never acceptable. The
> failure mode is a dark panel plus `[RDP] refusing to power the panel: no factory
> VCOM from the PMU` on the console — recoverable, unlike a wrong VCOM. This
> deliberately diverges from the reference firmware, which drives 1290 mV
> unconditionally.

### 1.5 I²C devices

One shared bus: **SCL = GPIO40, SDA = GPIO39, 400 kHz**
(`read_pico_firmware/README.md` Pinout; `read_pico_board.c` `board_init`).

| Device | 7-bit addr | Status | Evidence |
|---|---|---|---|
| CST836U touch | `0x15` | V | `components/cst836u/include/cst836u.h` (`CST836U_ADDR_DEFAULT`); `read_pico_board.c` `i2c_dev_name` and `s_census_map` |
| SC7A20H accelerometer | `0x19` | V | `components/sc7a20h/include/sc7a20h.h`; `read_pico_board.c` census map |
| FCA9555 expander | `0x24` (base `0x20`; A2=1, A1=0, A0=0) | V | `components/fca9555/include/fca9555.h` (`FCA9555_ADDR_BASE 0x20`, `FCA9555_ADDR_DEFAULT 0x24`, `FCA9555_ADDR_FROM_A`); `read_pico_board.c` selftest log string |
| CW32L010 PMU | `0x2A` | V | `components/read_pico_pmu/include/read_pico_pmu_protocol.h` (`PMU_I2C_ADDR 0x2A`); `read_pico_pmu/README.md` |
| SY7636A EPD PMIC | `0x62` | V | `components/sy7636a/include/sy7636a.h` (`SY7636A_ADDR_DEFAULT 0x62`) |
| Battery gauge / charger IC | — | U | None verified. The PMU reports battery voltage and charge state; no separate gauge or charger register map was found on this bus |
| Any other address | — | U | Not enumerated. `read_pico_board.c`'s bus scan accepts `0x08..0x77` and names only the five entries above; an unknown `ACK` is logged without a name |

Bus-recovery note worth keeping: a USB/soft reset does not power-cycle the
slaves, and a stuck CST836U or CW32 can hold SDA so that **every** probe times
out (not NACKs). `read_pico_board.c` contains a 9-clock + STOP recovery routine
and re-probes after `i2c_master_bus_reset`.

### 1.6 Touch (CST836U)

| Fact | Value | Evidence |
|---|---|---|
| Address / bus | `0x15`, shared bus SDA39/SCL40, 400 kHz | `cst836u.h`; `read_pico_board.c` |
| Points | up to **2** simultaneous points; a 15-byte frame | `cst836u.h` (`CST836U_MAX_POINTS 2`, `CST836U_RAW_LEN 15`) |
| Data register | reads register `0x00` (touch data); `0xA6` is the info register | `cst836u.c` (`CST836U_REG_TOUCH_DATA 0x00`, `CST836U_REG_INFO 0xA6`) |
| Commands | normal `0xFE00`, deep sleep `0xA503` | `cst836u.c` |
| Point frame decode | `data[2] & 0x0F` = point count; each point is 6 bytes at `data[3 + i*6]`: byte0 bits[7:6] = event, bits[3:0] = X high nibble; byte1 = X low; byte2 bits[7:4] = id, bits[3:0] = Y high nibble; byte3 = Y low | `cst836u.c` (`cst836u_read`) |
| INT# | GPIO43, **open-drain, active-low**, 10 k pull-up on the FPC. Not an RTC-capable S3 pin | `components/read_pico/include/read_pico_board.h`; `README.md` Pinout |
| Reset | FCA9555 **P0.7**, active-low; 10 ms low then release; caller then waits for boot | `read_pico_board.c`; `cst836u.c` |
| Power | always powered; no reset GPIO (reset is an expander line) | `read_pico_init.c` (`rst_gpio = GPIO_NUM_NC`, `reset_fn = read_pico_touch_reset`) |
| Modes | dynamic report ≈ 5 mA, deep sleep ≈ 10 µA with RST as the only wake | `cst836u.h` |
| **Recover-after-deep-sleep rule** | After deep sleep the chip ignores I²C entirely. `cst836u_read()` returns an empty point set while in that mode so the caller's loop can still time out and reset; **the RST pulse is the only way back**, and the caller must wait for boot after it | `cst836u.h`; `cst836u.c`; `read_pico_board.h` comment |
| Datasheet standby / gesture wake | **Not implemented** in the reference driver | `cst836u.h` header comment |

### 1.7 Accelerometer (SC7A20H)

| Fact | Value | Evidence |
|---|---|---|
| Address / INT | `0x19`; INT1 = **GPIO1** | `sc7a20h.h`; `read_pico_init.c` (`READ_PICO_ACCEL_INT1 GPIO_NUM_1`) |
| Identity | `WHO_AM_I = 0x11`, `VERSION = 0x28` | `sc7a20h.h` (`SC7A20H_WHO_AM_I_VAL`, `SC7A20H_VERSION_VAL`) |
| Bring-up | soft reset `0x68 = 0xA5`, then WHO_AM_I/VERSION, then `CTRL5.BOOT` | `sc7a20h.h` header comment |
| Capability | 3 axes; tap (AOI), orientation (6D), free-fall, FIFO; **no gyroscope** | `sc7a20h.h`; `sc7a20h_lab.h` (lab helpers); `read_pico_firmware/README.md` |
| Idle config | ODR 12.5 Hz, ±2 g, LP mode, OSR/DLPF/HPF off, XYZ | `sc7a20h.h` (`SC7A20H_CONFIG_IDLE`) |
| Pickup wake | motion threshold 350 mg over 3 samples, LP sampling + AOI1 high-event + HPIS1 so gravity does not trip a resting part; INT1 routed and latched | `main/sleep.c`; `main/apps/app_sleep.c` (the sleep page states 350 mg / 3 samples); `sc7a20h.h` (`sc7a20h_arm_pickup_wake`) |
| Bench at-rest (verified, from the board header) | chip frame X = **−5 mg**, Y = **−119 mg**, Z = **−1005 mg**; device frame is about **+Z face-up**. The board header stores the negated offsets: `ACCEL_ZERO_X_MG 119`, `ACCEL_ZERO_Y_MG 5`, `ACCEL_ZERO_Z_MG 1005` | `components/read_pico/include/read_pico_board.h` |
| Chip → device mapping | `Xd = −Yc`, `Yd = −Xc`, `Zd = −Zc` (applied to both mg and raw) | `components/read_pico/read_pico_init.c` (`read_pico_accel_to_device`) |
| Power policy in the reference | sampled only while the accelerometer page is open; powered down (`CTRL1 ODR = 0000`, ≈ 0.5 µA) on leave and right after the boot identity check | `read_pico_init.c`; `read_pico_init.h`; `sc7a20h.h` |

### 1.8 Storage, buzzer, keys and power

| Area | Fact | Status | Evidence |
|---|---|---|---|
| SD transport | 1-bit SDMMC, CLK **38** / CMD **42** / D0 **44**, `SDMMC_FREQ_HIGHSPEED` (40 MHz), `SDMMC_SLOT_FLAG_INTERNAL_PULLUP`, `cd = GPIO_NUM_NC`, `wp = GPIO_NUM_NC` | V | `components/read_pico/read_pico_sd.c` |
| SD card detect | FCA9555 P0.6, active-low; a failed I²C read returns "absent", never "present" | V | `read_pico_board.c` |
| SD first-mount quirk | the reference retries once after a 200 ms delay because the first clock negotiation after power-up often times out | V | `read_pico_sd.c` |
| Buzzer | GPIO2 → AO3400A gate (R8 = 100 k pulldown), high = on, low = off, idle duty 0; LEDC timer0/channel0, 10-bit; nominal carrier `80 MHz / 1024` rounded down to a multiple of 1 kHz = 78 kHz | V | `components/read_pico/read_pico_buzzer.c` |
| Buzzer topology | `SYS_VDD → buzzer → drain`, D1 freewheel. It is **not** a DC-blocked / LPF audio stage, so a plain tone is the intended use | V | `read_pico_buzzer.c` header comment |
| Key zones | three capacitive zones **below the display** inside the taller touch panel, undrawn, hit-tested by raw coordinate; KEY1/KEY3 page multi-page screens and KEY2 always forces a full GC16 redraw | V | `main/ui/ui_menu.h`; `main/app/app_loop.c`; `read_pico_firmware/README.md` ("Three key zones") |
| Power key | PMU-reported (`key_raw_events`, `STATUS` level, DOWN/UP/SHORT/LONG/FORCE_OFF); the host never sees a GPIO | V | `main/apps/app_key.c`; `read_pico_pmu_protocol.h` |
| Light sleep | `gpio_wakeup_enable(GPIO41, GPIO_INTR_LOW_LEVEL)` + `esp_sleep_enable_gpio_wakeup()`; optionally also GPIO1 (SC7A20H INT1, high level) when pickup wake is on; entered with `esp_light_sleep_start()` | V | `main/sleep.c` (`lock_arm_ioe_wakeup`, `app_light_sleep_wait`) |
| Deep sleep / "off" | `app_enter_host_sleep()`: `APP_SLEEP_OFF` → `read_pico_pmu_power_off()` (`REQUEST_OFF` → `SHUTDOWN_READY`, EN stays low); `APP_SLEEP_DEEP` → `read_pico_pmu_report_sleep()` (`HOST_SOFT_SLEEP`, waits for `STATUS == SOFT_SLEEP`, then EN drops). The host then spins forever | V | `main/sleep.c`; `read_pico_pmu/README.md` |
| Real deep-sleep wake sources | **None on the ESP side.** Power comes back when the *PMU* re-enables the host: power-key short press, `wake_on_charge` (AC-in), or an RTC alarm. Alarm-fired wakes the host with `wake_reason = 6` | V | `read_pico_pmu/README.md`; `read_pico_pmu_protocol.h` (`PMU_CMD_ALARM_SET`, `ALARM_FIRED`); `main/apps/app_sleep.c` (wake chips: Key / Pickup / Alarm / AC On; boot-reason list incl. "By AC On", "By Alarm") |
| Modes the sleep page exposes | lock-screen light sleep, deep sleep, power off; `wake_on_charge` and alarm arming are PMU config | V | `main/apps/app_sleep.c`; `read_pico_pmu.h` (`wake_on_charge`, `alarm_mode`) |
| PMU battery data | `battery_mv`, `battery_raw`, `soc_permille`, `charge_state`, plus a fast `qb_mv`/`qb_soc`/`qb_charge` snapshot | V | `components/read_pico_pmu/include/read_pico_pmu.h` (`pmu_snapshot_t`) |
| PMU USB / VBUS detect | No dedicated detect pin verified. Charge state comes from the PMU; whether a VBUS-present signal exists beyond `charge_state` | U | Not found in the fetched sources |
| PMU indicator LED | PMU-owned (`led_state`, LED commands) | V | `read_pico_pmu.h`; `read_pico_pmu_protocol.h` (LED command group) |

## 2. Selected route and rationale

### 2.1 Closest supported path

Per [`port-device-bsp`](../../.agents/skills/port-device-bsp/SKILL.md) "Choose the
smallest supported path" (`SKILL.md:46-64`), the pinned SDK already contains the
exact display class this board needs, so the port is a **new board profile +
board-support library + one compatible config extension** — not a new driver.

| Finding | Route taken |
|---|---|
| SDK already supports the display class | Reuse `DisplayController::LgfxEpd` + `LgfxEpdDriver` (raw parallel EPD via LovyanGFX `Panel_EPD`/`Bus_EPD`) |
| New board with supported controllers | Add `FREEINK_DEVICE_READPICO`, `BoardProfile READ_PICO`, and a `BoardReadPico` board-support library |
| One demonstrated driver gap | `LgfxEpdConfig` carries only 8 data pins and hardcodes `bus_width = 8`; extend it compatibly ([§2.3](#23-the-lgfxepdconfig-extension)) |
| Unsupported chip architecture | Not applicable — ESP32-S3 is a first-class family (`BoardConfig.h:100-111`) |

This is also the rule from
[`device-variants.md`](device-variants.md): the X3/X4 ESP32-C3 image is untouched,
and this board gets its **own profile and build environment** exactly like the
eight other S3 targets (`device-variants.md:25-38`, `:270-291`; golden rule #1 in
[`AGENTS.md`](../../AGENTS.md#L37)). A shared `DeviceType` enum member is
explicitly wrong (`port-device-bsp/SKILL.md:59-64`).

### 2.2 Why `LgfxEpdDriver`, not an SPI controller driver

The E0470A01 has **no on-glass controller and no controller RAM**. Every row is
clocked by the MCU over the S3's LCD (i80) peripheral and the waveform rails come
from an external PMIC (`read_pico_firmware/README.md` Display row;
`components/epdiy/src/output_lcd/`). That is precisely the class
`LgfxEpdDriver` documents for itself:

> "For panels with NO on-glass controller/RAM — the MCU clocks every row/column
> over the ESP32-S3 LCD (i80) peripheral and an external PMIC generates the
> waveform rails. […] it can't use `EpdBus`, so `usesExternalBus() == true` and
> LovyanGFX's `Panel_EPD`/`Bus_EPD` own the bus"
> — `freeink-sdk/libs/display/FreeInkDisplay/src/driver/LgfxEpdDriver.h:3-20`

Concretely:

- `usesExternalBus() == true` (`LgfxEpdDriver.h:34`) makes
  `FreeInkDisplay::begin()` skip FreeInk's own bus bring-up
  (`FreeInkDisplay.cpp:211-222`) — correct, because there is no SPI EPD bus.
- The facade reaches the driver through the existing
  `#elif FREEINK_DRIVER_LGFX_EPD → _driver = &lgfxEpdDriver();` branch
  (`FreeInkDisplay.cpp:196-197`); no new `selectDriver()` case is needed.
- Peer precedent in-tree: `BoardConfig::LILYGO_T5S3` (`BoardConfig.h:1133-1175`)
  and `BoardConfig::M5PAPER_S3` (`:1268-1307`), both with
  `DisplayController::LgfxEpd` and no SPI display pins, plus the API-level doc
  `freeink-sdk/docs/lilygo-t5s3-support.md`.
- The SPI-controller path (`Ssd1677Driver`, `Uc8xxxDriver`, `Ed2208`, and the
  `EpdBus` layer) cannot express this panel: it assumes a command/data SPI
  controller with its own RAM, LUT upload, BUSY pin and reset. None of those
  exist here.

### 2.3 The `LgfxEpdConfig` extension

LovyanGFX already supports a 16-bit bus; the SDK's config does not pass it
through.

LovyanGFX `Bus_EPD::config_t` (`lovyan03/LovyanGFX`,
`src/lgfx/v1/platforms/esp32/Bus_EPD.h`) is:

```cpp
uint32_t bus_speed;
union { int8_t pin_data[16]; struct { int8_t pin_d0 … pin_d15; }; };
union { int8_t pin_ctrl[7];  struct { int8_t pin_pwr, pin_sph /*XSTL*/, pin_spv,
                                             pin_oe /*XOE*/, pin_le /*XLE*/,
                                             pin_cl /*XCL*/, pin_ckv; }; };
uint8_t bus_width;
```

so `int8_t pin_data[16]`, `pin_ctrl[7] = {pwr, sph(XSTL), spv, oe(XOE), le(XLE),
cl(XCL), ckv}` and an explicit `bus_width` are all already there.

The SDK's `LgfxEpdConfig` carries only `int8_t dataPins[8]` and no bus width
(`freeink-sdk/libs/display/FreeInkDisplay/include/LgfxEpdConfig.h:18-39`), and
`FreeInkLgfxEpd::setup()` copies exactly 8 pins and then **hardcodes**
`bc.bus_width = 8` (`LgfxEpdDriver.cpp:58`, `:66`). Two new members are enough for
this board.

**Rejected: `int8_t dataPins[16]`.** Widening the existing array to 16 in place
breaks both existing initializers. `LgfxEpdConfig` is brace-initialized
positionally in two places (`BoardT5S3/src/LilyGoT5S3LgfxConfig.cpp:179` and
`BoardPaperS3/src/M5PaperS3LgfxConfig.cpp:21`), so a 16-wide first array would
silently consume the following `pinSph…pinPwr` values as `dataPins[8..15]` and
then fail on a narrowing conversion — a loud failure, but still a change to two
working boards for zero benefit. The port therefore appends:

```cpp
// LgfxEpdConfig.h — appended after lutFastestStep so existing brace
// initializers stay valid and keep their meaning.
int8_t dataPinsHigh[8] = {PIN_UNASSIGNED, …};  // D8..D15; used only when busWidth == 16
uint8_t busWidth = 8;                          // 8 = existing boards, 16 = Read Pico
```

and `FreeInkLgfxEpd::setup()` becomes:

```cpp
for (int i = 0; i < 8; ++i) bc.pin_data[i] = c.dataPins[i];
for (int i = 8; i < c.busWidth && i < 16; ++i) bc.pin_data[i] = c.dataPinsHigh[i - 8];
bc.bus_width = c.busWidth;
```

This keeps `BoardT5S3` and `BoardPaperS3` byte-identical and satisfies
`port-device-bsp`'s "prefer board configuration or a compatible extension that
preserves existing entrypoints" (`SKILL.md:66-70`).

### 2.4 What LovyanGFX actually does with those pins (verified, and a gap)

Read from `Bus_EPD::init()`, `beginTransaction()`, `writeScanLine()`,
`endTransaction()` and `notify_line_done()` in `Bus_EPD.cpp`:

| LovyanGFX config field | How the hardware is driven |
|---|---|
| `pin_data[0..bus_width-1]` | i80 data lines (`bus_config.data_gpio_nums`), output forced non-open-drain before `esp_lcd_new_i80_bus` |
| `pin_cl` | i80 write strobe (`wr_gpio_num`) = XCL |
| `pin_sph` | i80 **CS** (`io_config.cs_gpio_num`) = XSTL |
| `pin_pwr` | i80 **DC** (`bus_config.dc_gpio_num`, "dummy setting") |
| `pin_spv` | plain GPIO: driven low → high once per transaction in `beginTransaction()` |
| `pin_ckv` | plain GPIO: raised in `writeScanLine()`, lowered in the trans-done ISR |
| `pin_le` | plain GPIO: lowered in `writeScanLine()`, raised in the trans-done ISR (latch) |
| `pin_oe` | `pinMode()` at `init()`, then only in the stock `powerControl()` sequence |

Consequences this port must live with:

1. **`pin_oe` and `pin_pwr` have no GPIO on this board.** XOE is FCA9555 P0.1 and
   the PMIC enable is P0.3 (§1.4). Both must therefore be handled by the board's
   `LgfxEpdPowerHooks`. The `FreeInkBusEPD::powerControl()` override replaces the
   stock sequence whenever a hook is supplied (`LgfxEpdDriver.cpp:37-50`), so the
   hook owns the **entire** rail order — including SPV, which the stock sequence
   would otherwise drive (M5GFX PaperS3 relies on that stock sequence:
   `M5PaperS3LgfxConfig.cpp:6-8`).
2. **`init()` still calls `lgfx::pinMode()` on `pin_oe` and `pin_pwr`.** Passing
   `PIN_UNASSIGNED` (−1) may or may not be tolerated — `gpio_hi`/`gpio_lo` guard
   `pin >= 0` (`common.hpp`), but `pinMode` is a separately defined function and
   this round could not read its body (GitHub's unauthenticated API rate limit,
   then an unreachable `raw.githubusercontent.com`). LilyGo sidesteps the question
   by passing a harmless real GPIO (its LoRa CS) as a dummy for both
   (`freeink-sdk/docs/lilygo-t5s3-support.md:37-45`). This board has **no obvious
   spare**: see blocker B2. Note `pin_pwr` doubles as the i80 DC line, so a dummy
   must be a pin with no board function at all.
3. **XSTL is driven as an i80 CS, not as a DE signal.** epdiy wires XSTL to the
   LCD's DE output (`read_pico_board.c` `board_poweron`) and documents L_SL as a
   start-pulse window with `T(L_SL) ≥ 0.3 µs`; LovyanGFX asserts CS for the whole
   line transfer instead. Both produce a per-line start pulse, but the polarity
   and width differ. Whether the E0470A01 accepts the CS form is **not
   verifiable from source** (§5, blocker B4).
4. **The vendor's line timing is not expressible.** `LgfxEpdConfig` exposes only
   `busHz` and `linePadding`; the i80 peripheral derives the rest of the line
   time. So the epdiy numbers in §1.3 (L_SL/L_BL/L_EL clock counts, CKV high
   width, the 11 090 µs frame target) cannot be handed to `Panel_EPD` directly.
   They remain the reference for judging whether the Lgfx path is close enough,
   and the fallback is an SDK-side extension of `Bus_EPD`/`Panel_EPD` behaviour
   (§5, blocker B4).

### 2.5 Waveform tables: correspondence, and what must be re-verified

**epdiy format (what the board ships).** The E0470 tables are
`const uint8_t [frames][16][4]` — in `gc16.h` the declaration is
`static const uint8_t e0470_full_gc16_data[E0470_FULL_GC16_FRAMES][16][4]`
(`components/e0470_epaper_waveform/waveforms/gc16.h`) — exposed through
`EpdWaveformPhases::luts` as a flat `const uint8_t*`
(`components/e0470_epaper_waveform/e0470_epaper_waveform.c:185-195`). The indexing
is explicit in that file:

```c
/* :32  */ data[f][to][from / 4] |= action << (6 - 2 * (from % 4));   // lut_or
/* :231 */ phases->luts + ((size_t)phase * 16 + to) * 4 + from / 4;   // e0470_phase_action
```

So one **frame** is 16 (`to`) × 4 bytes, each byte holding four 2-bit `from`
actions with `from % 4 == 0` in bits [7:6] (MSB-first). That is a
**16 × 16 (from → to) action matrix = 64 bytes per frame**.

**LovyanGFX format.** LUTs are `const uint32_t[]` with `*_step` = the number of
entries = the number of frames (`Panel_EPD.hpp` `config_detail_t`), and each entry
packs 16 × 2-bit actions, level `L` at bits `[2L+1 : 2L]`
(`Panel_EPD.cpp`, `LUT_MAKE(d0…d15) = (d0<<0)|(d1<<2)|…|(d15<<30)`). That is
**16 actions = 4 bytes per frame**, i.e. **16× smaller** than the epdiy frame.

The expansion loop is the authoritative read (`Panel_EPD.cpp:276-283`):

```cpp
for (int step = 0; step < lut_step; ++step) {
  auto lu = lut_src[0];
  for (int lv = 0; lv < 256; ++lv) {
    dst[lindex] = (((lu >> ((lv >> 4) << 1)) & 3) << 2) + ((lu >> ((lv & 15) << 1)) & 3);
    ++lindex;
  }
  ++lut_src;
}
```

`lv` is one byte of the **step framebuffer** = two packed 4-bit pixel levels, and
each nibble indexes the *same* 16-entry vector. So LovyanGFX's action is a
function of **the pixel's own current level** — a 1-D vector — while the
destination is whatever the accumulated frame sequence drives the pixel to.

**Conclusion and caveat (do not skip this).**

- The frame **count** maps 1:1 (`lut_*_step` = `frames`), and the *bits per frame*
  are the same (16 × 2 bits), so a mechanical conversion exists.
- The epdiy frame is a **2-D (from, to)** matrix and the LovyanGFX frame is a
  **1-D (from)** vector. A raw reinterpretation is **wrong twice over**: four
  consecutive epdiy bytes would become one `uint32_t` with the 2-bit group order
  reversed, *and* the `to` axis has no counterpart. On little-endian RISC-V
  "4 consecutive bytes = 1 `uint32_t`" is true of the *storage*, but that does not
  make the two layouts equivalent.
- **Before relying on byte-equivalence, the exact LUT read semantics in
  `Panel_EPD.cpp` must be re-verified against the pinned `m5stack/M5GFX @ 0.2.20`
  vendored LovyanGFX revision**, not only against `lovyan03/LovyanGFX` `master`.
  What was verified here is the upstream `master` source; the SDK consumes M5GFX
  `0.2.20` (`freeink-sdk/platformio.sample.ini:162`, `:219`), and the LUT model
  above is the load-bearing assumption for every waveform decision below.

**Documented waveform families** (`components/e0470_epaper_waveform/README.md`;
`.../include/e0470_epaper_waveform.h`). "Changing panel waveforms voids the
warranty." (`waveforms/*.h` are data tables and must not be hand-edited.)

| Symbol | Content | Intended use |
|---|---|---|
| `E0470_WAVEFORM` | Default. GC16 **36** phases / GL16 **37** phases, **derived at boot** by the trimmer, plus a 50/50 **threshold DU** (dest 0–7 → black, 8–15 → white). Trimmer defaults `erase_max = 11`, `sat_cut = 5`, `white_sat_cut = 0`, `hold = 3` | Page refresh in the reference firmware |
| `E0470_FULL_WAVEFORM` | Full tables: GC16 **48** / GL16 **48** phases, DU **20** phases, **one 0–50 °C** temperature range. 17-step white-push ladder → 11 distinguishable grays after collapse. Kept upstream as the A/B reference | Reference / A/B comparison |
| `E0470_GRAY8_WAVEFORM` | 8 distinguishable grays, GC16 / GL16 **30** phases; the component README states about **360 ms** per full refresh versus about **430 ms** for the default table | Optional faster page mode |
| `E0470_FOLLOW_WAVEFORM` | 8-frame short DU (black **7** / white **8**) built at boot by `e0470_follow_lut_build()`, meant for the **FAST** scan profile | Touch ink / live digits |

Supporting helpers: `e0470_waveform_init()` (builds the default and follow tables;
call once before the first refresh), `e0470_waveform_phases()` (MODE_GC16 /
MODE_GL16 / MODE_DU lookup), `e0470_phase_action()`, and `e0470_waveform_trim()`
(`components/e0470_epaper_waveform/e0470_waveform_trim.c`, declared in
`include/e0470_waveform_trim.h`). Frame period is fixed at
`E0470_WAVEFORM_FRAME_US = 11090` µs (ν ≈ 90 Hz) — "the design point in the
panel's algorithm document"; scan timing is owned by `read_pico`, not by the
waveform component. Derived wall time is `N × T_frame` (the header's own
`read_pico_epd_refresh_ms`); the ~430 ms / ~360 ms figures above are the
component's stated values, **not** measurements made here.

**Frozen choice for this port: start from `E0470_FULL_WAVEFORM`.** Rationale and
trade-off:

- It is the **vendor-authored, untrimmed** data set that upstream keeps
  deliberately as the A/B reference, so nothing has to be re-derived: no boot-time
  trim run, no re-implementation of `e0470_waveform_trim()`, and no dependence on
  a trim parameter set that upstream itself says "should be tuned on hardware
  against ghosting".
- Cost: GC16 48 versus 36 phases and GL16 48 versus 37 phases. Per the header's
  own formula that is more refresh wall time and more drive per refresh, and more
  LUT steps (see §2.6). Whether the extra phases buy anything on CrossMux's
  content is exactly what a hardware A/B must decide.
- The trimmed `E0470_WAVEFORM` path (and the 8-gray table) stay available as
  later options once the LUT conversion is proven; adopting the trim now would
  stack two unverified transformations on top of each other.

### 2.6 RAM the Lgfx path will ask for

Computed from the allocation expressions (arithmetic, **not** measurements):

| Allocation | Expression | For 1216 × 684 | Pool |
|---|---|---|---|
| `_step_framebuf` | `(w·h/2) · 2 · sizeof(uint16_t)` (`Panel_EPD.cpp:232`) | 1,663,488 B ≈ 1.59 MiB | PSRAM |
| `_buf` | `(w·h)/2` (`Panel_EPD.cpp:235`) | 415,872 B ≈ 406 KiB | PSRAM |
| `_lut_2pixel` | `lut_total_step · 256 · sizeof(uint16_t)` (`Panel_EPD.cpp:228`) | 512 B × total steps — **internal DMA RAM** | internal |
| `_dma_bufs[2]` | `2 · (w/4 + line_padding)` (`Panel_EPD.cpp:238-240`) | ≈ 624 B | internal DMA |
| `LgfxEpdDriver` canvas | `w · h` at 8 bpp (`LgfxEpdDriver.cpp:127-140`) | 831,744 B ≈ 812 KiB | PSRAM |
| `g_lsb` + `g_msb` | `2 · (w/8) · h` (`LgfxEpdDriver.cpp:137-139`) | 207,936 B ≈ 203 KiB | PSRAM |
| facade framebuffer | `(w/8) · h` (`FreeInkDisplay.cpp:391-403`) | 103,968 B ≈ 101.5 KiB | PSRAM via `FREEINK_FB_PSRAM` |
| **PSRAM subtotal** | | **≈ 3.1 MiB of 8 MiB** | |
| **Internal DMA subtotal** | `_lut_2pixel` (`Panel_EPD.cpp:228`): the built-in tables are `eraser 4 + quality 32 + fastest 7`, plus the wired `GC16 49` (text) and `DU 21` (fast) = **113 steps** | **≈ 56.5 KiB** (130 steps ≈ 65.0 KiB if GL16 is wired as well) | |

Two consequences to carry into the implementation and its verification:

- `_lut_2pixel` is the one large **internal** allocation. Note that a null LUT
  pointer does **not** mean zero steps: `Panel_EPD::init()` substitutes a built-in
  table for any null pointer (`Panel_EPD.cpp:178-197`), so `lutFastest` left null
  still costs its built-in 7 steps ≈ 3.5 KiB, and the "drop `lutFastest` to save
  4 KiB" idea is not available without changing which mode carries which table.
  `_lut_2pixel` is over-allocated 2× upstream relative to what the expansion loop
  writes (`Panel_EPD.cpp:228` allocates `… · sizeof(uint16_t)` but only 256 of the 512
  bytes per step are written). That is third-party code pulled from the PlatformIO
  registry; it must not be patched locally — record it, do not change it.
- PSRAM is PSRAM heap (`MALLOC_CAP_SPIRAM`), not `.bss`, so internal-RAM headroom
  is unaffected by the display buffers — but the ~3.1 MiB PSRAM budget needs an
  actual watermark measurement ([§4](#4-verification-status)).

### 2.7 Not the C3 image, and not the 120 MHz timing

Per [`device-variants.md`](device-variants.md) this target shares **nothing** with
the X3/X4 ESP32-C3 image: different MCU family, different flash/PSRAM, different
partition table, different display class, different input model, different power
model. It gets its own hardware profile and its own build environment
(`device-variants.md:25-38`, `:270-291`). No member is added to the X3/X4
`DeviceType` enum, and no runtime detection is introduced.

The reference firmware's **120 MHz flash + PSRAM** configuration is **not**
adopted in this round. Reasons (all cited):

- IDF's temperature compensation for the PSRAM timing point is deliberately
  disabled upstream because this board's flash vendor (Zbit `0x5E`) is not in
  IDF's verified list; enabling it aborts in `do_system_init_fn`. The
  configuration therefore runs with **no temperature protection** — a documented
  experimental risk (`sdkconfig.defaults` comment block;
  `read_pico_firmware/README.md`).
- It is explicitly flash-part dependent, and `sdkconfig.ci` uses default timing
  precisely because the timing is not portable (`sdkconfig.defaults`;
  `dot_web_docs/zh-Hans-CN/read_0/firmware.mdx` warning callout).
- It needs `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` plus a vendor-specific HPM patch
  (`components/read_pico/read_pico_flash_hpm.c`), which does not compose with the
  `firmware_tuned` custom-SDK rebuild this port uses.

The cost is real and must be stated: the reference firmware's own comment says the
80 MHz effective PSRAM bandwidth (≈ 32 MB/s) is the refresh ceiling because the
renderer streams the framebuffer out of PSRAM row by row. Staying at default
timing therefore means slower refreshes than the vendor firmware until the 120 MHz
configuration is evaluated on real hardware (§5, blocker B8).

## 3. Frozen interface specification

Downstream tasks follow these names **verbatim**. Anything not listed here is
implementation freedom.

### 3.1 `BoardConfig.h` — build composition

| Symbol | Frozen change |
|---|---|
| `FREEINK_DEVICE_READPICO` | New; `#ifndef … #define FREEINK_DEVICE_READPICO 0`, added alongside the existing `FREEINK_DEVICE_*` block (`BoardConfig.h:83-85`) |
| device coherence `#if` | Add `\|\| FREEINK_DEVICE_READPICO` to the list at `BoardConfig.h:88-92`, and to the `#error` text at `:93-94` |
| `FREEINK_MCU_S3` | Add `FREEINK_DEVICE_READPICO` to the list (`BoardConfig.h:102-106`) |
| `FREEINK_DRIVER_LGFX_EPD` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:183-187`) |
| `FREEINK_CAP_TOUCH` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:206-211`) |
| `FREEINK_CAP_RTC` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:292-297`) |
| `FREEINK_CAP_IMU` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:301-303`) |
| `FREEINK_CAP_BUZZER` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:308-311`) |
| `FREEINK_FB_PSRAM` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:329-331`) — the 103,968-byte 1-bpp framebuffer is PSRAM-first |
| `FREEINK_SD_SDMMC` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:338-342`) |
| `FREEINK_BATTERY_I2C_GAUGE` | Add `\|\| FREEINK_DEVICE_READPICO` (`BoardConfig.h:272-276`) |
| Not changed | `FREEINK_CAP_FRONTLIGHT`, `FREEINK_CAP_WARMLIGHT`, `FREEINK_CAP_USB_MSC`, `FREEINK_CAP_HAPTIC`, `FREEINK_CAP_AUDIO`, `FREEINK_CAP_MIC`, `FREEINK_CAP_LED`, `FREEINK_CAP_COLOR`, `FREEINK_CAP_TEMP_HUMIDITY`, `FREEINK_LOG_TRANSPORT` all keep their defaults (all off / `FREEINK_LOG_TRANSPORT_SERIAL` for this device) |

Capability style note: every macro above follows the file's existing
`#ifndef X / #define X (FREEINK_DEVICE_… || …)` defaulting pattern, so any of them
can still be overridden with `-DFREEINK_…=0/1`.

Frozen static guarantees (add next to the existing `static_assert`s):

```cpp
static_assert(READ_PICO.displayWidth / 8 * READ_PICO.displayHeight == 103968,
              "Read Pico must use one 103,968-byte framebuffer (1216/8 x 684)");
static_assert(READ_PICO.displayController == DisplayController::LgfxEpd &&
                  READ_PICO.display.sclk == PIN_UNASSIGNED,
              "Read Pico has no SPI display pins; the parallel bus lives in LgfxEpdConfig");
static_assert(READ_PICO.sdmmc.busWidth == 1 && READ_PICO.sdmmc.d1 == PIN_UNASSIGNED,
              "Read Pico SD is 1-bit SDMMC");
static_assert(READ_PICO.touch.controller == TouchController::Cst836u &&
                  READ_PICO.touch.irq == 43 && READ_PICO.touch.irqActiveLow,
              "Read Pico CST836U INT# is GPIO43, open-drain active-low");
```

### 3.2 Enums and accessors

| Enum | Frozen value | Decision and justification |
|---|---|---|
| `Board` | **`ReadPico`** appended after `MetalioEink4` (`BoardConfig.h:380-399`) | One profile per S3 target, mirroring `Sticky` / `MetalioEink4` |
| `InputStyle` | **`ReadPicoTouchStrip`** appended after `OnePageAdcLadder` (`BoardConfig.h:402-411`) | No existing value describes "no GPIO buttons at all; navigation comes from touch plus three capacitive zones inside the *extended* touch frame, and the power key is PMU-reported". `DigitalButtons` would be vacuous-but-misleading. Consumers compare against specific values (`InputManager.cpp:103-607`), so appending changes no existing branch |
| `DisplayController` | **reuse `LgfxEpd`** (`= 4`) | The panel is the same driver class: raw parallel, no on-glass controller, `usesExternalBus() == true` (`LgfxEpdDriver.h:3-34`). Adding an enumerator would need a second driver class and a new `selectDriver()` branch for identical behaviour — rejected under `port-device-bsp/SKILL.md:66-70`. The profile's `displayController` is what `selectDriver()`/`FreeInkDisplay.cpp:196-197` documents, and it is what existing LgfxEpd profiles set (`BoardConfig.h:1138`, `:1272`) |
| `TouchController` | **`Cst836u`** appended after `Cst816s` (`BoardConfig.h:435`) | New backend required: different I²C address (`0x15`), different data register (`0x00` vs CST816S's `0x02`), a 6-byte-per-point frame, two points, and a reset that lives on an expander. `Cst816s` cannot be reused |
| `ImuType` | **`Sc7a20h`** appended after `Qmi8658` (`BoardConfig.h:630`) | New backend: different address (`0x19`), `WHO_AM_I` `0x11` vs QMI8658's `0x05`, different register map, and **accelerometer only** — the backend must report `gx = gy = gz = 0` (`Imu::read()` shape: `Imu.cpp:171-203`) |
| `RtcType` | **`Cw32L010Pmu`** appended after `Rx8010` (`BoardConfig.h:629`) | The PMU RTC is **not** a register map: it is reached through the PMU frame protocol (unix seconds + alarm mode/target; `read_pico_pmu.h`, `read_pico_pmu_protocol.h`). A new value plus a board hook is needed; map onto an existing PCF/DS/RX type would be an invention |
| `GaugeType` | **`Cw32L010Pmu`** appended after `Cw2017` (`BoardConfig.h:491`) | Same reason: `BatteryMonitor` dispatches on `gaugeType` and does raw register reads (`BatteryMonitor.cpp:208-286`); the PMU needs a frame protocol |
| `is*()` accessor | **`inline bool isReadPico() { return ACTIVE.board == Board::ReadPico; }`** next to the `isOnePage()`-style siblings (`BoardConfig.h:1962-1974`) | |
| `DEFAULT_DEVICE` | Add `#elif FREEINK_DEVICE_READPICO → constexpr BoardProfile DEFAULT_DEVICE = READ_PICO;` to the chain (`BoardConfig.h:1814-1849`) | |
| `selectDevice()` | Add `#if FREEINK_DEVICE_READPICO / case Board::ReadPico: ACTIVE = READ_PICO; break; / #endif` (`BoardConfig.h:1861-1960`) | |
| `MAX_FRAMEBUFFER_BYTES` | Add a `FREEINK_DEVICE_READPICO ? panelBytes(READ_PICO) : 0u` term (`BoardConfig.h:1789-1809`) | Keeps one-device builds sized to exactly the panel |
| `holdPowerRails()` | **No change.** `power = {}` (no latch, no charge enable): the PMU owns the host enable rail | |

### 3.3 `BoardProfile READ_PICO`

Frozen initializer (`BoardConfig.h`, placed after `METALIO_EINK4`):

```cpp
// Read Pico (RDP-G01-W) — ESP32-S3 N16R8, E0470A01 684x1216 raw-parallel EPD
// (16-bit bus, SY7636A rails behind an FCA9555), CST836U touch + 3 capacitive
// key zones, SC7A20H accelerometer, CW32L010 PMU, 1-bit SDMMC, GPIO2 buzzer.
// Sources: MindReset/read_pico_firmware README + components/{read_pico,
// read_pico_pmu,cst836u,sc7a20h,fca9555,sy7636a,e0470_epaper_waveform,epdiy},
// main/{apps,ui,sleep.c}. See docs/engineering/read-pico.md.
constexpr AudioConfig READ_PICO_AUDIO = {AudioOutput::None,    PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
                                         PIN_UNASSIGNED,       PIN_UNASSIGNED, true,           PIN_UNASSIGNED,
                                         PIN_UNASSIGNED,       PIN_UNASSIGNED, 0,              2};

constexpr BoardProfile READ_PICO = {
    Board::ReadPico,
    "read_pico",
    InputStyle::ReadPicoTouchStrip,
    DisplayController::LgfxEpd,
    1216,  // displayWidth: source columns (epdiy E0470_DISPLAY.width)
    684,   // displayHeight: gate lines  (epdiy E0470_DISPLAY.height)
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
     PIN_UNASSIGNED},  // no SPI display pins: the 16-bit i80 bus lives in LgfxEpdConfig
    0,                // displaySpiHz n/a (external bus)
    // SPI view of the SD slot is unused; SD is 1-bit SDMMC (see sdmmc below) and
    // the card has no power gate (powerEnable unassigned).
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, false, 0},
    {PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED,
     PIN_UNASSIGNED, false},  // no GPIO buttons: keys are the touch strip + the PMU power key
    PIN_UNASSIGNED,  // batteryAdc: none — the CW32L010 PMU reports battery/SoC
    PIN_UNASSIGNED,  // batteryChargeStatus: none — charge state comes from the PMU
    1.0f,            // batteryDividerMultiplier: unused (no ADC path)
    PIN_UNASSIGNED,  // usbDetect: not verified; charge state comes from the PMU
    // CST836U: SDA39 SCL40 INT43 (open-drain, active-low, 10k pull-up on the FPC),
    // address 0x15, two points, no reset GPIO (FCA9555 P0.7 -> BoardReadPico).
    // rawMax* describe the PORTRAIT glass face (684 x 1216); the touch plane is
    // taller than the display and the three capacitive key zones live below it
    // (main/ui/ui_menu.h: y > 1300, centres x=80/240/400).
    {TouchController::Cst836u, 39, 40, 43, PIN_UNASSIGNED, 0x15, 0, 683, 0, 1215, false, 0, true, false,
     PIN_UNASSIGNED, false, false, false, false, true},
    NO_FRONTLIGHT,
    READ_PICO_AUDIO,  // AudioOutput::None; LEDC buzzer on GPIO2 (Buzzer lib)
    NO_LEDS,
    NO_FLIP,  // panel mount transform pending hardware; see docs/engineering/read-pico.md
    // 1-bit SDMMC: CLK38 CMD42 D0=44. D1/D2/D3 unused. No power gate and no CD
    // pin — card detect is FCA9555 P0.6 read by BoardReadPico.
    {38, 42, 44, PIN_UNASSIGNED, PIN_UNASSIGNED, PIN_UNASSIGNED, 1},
    // CW32L010 PMU at 0x2A on the shared bus SDA39/SCL40, 400 kHz: battery mV,
    // soc_permille and charge_state over the PMU frame protocol. No charger IC.
    {39, 40, 400000, 0x2A, 0, 0, GaugeType::Cw32L010Pmu},
    NO_MIC,
    // SC7A20H at 0x19 (WHO_AM_I 0x11). The CW32L010 (0x2A) also carries the RTC
    // and alarms, reached through the PMU protocol rather than a register map.
    {39, 40, 400000, 0x2A, 0, 0x19, 0, RtcType::Cw32L010Pmu, ImuType::Sc7a20h},
    1.2f,  // uiScale: touch device — STARTING VALUE, pending on-hardware measurement
    {},    // power: no latch, no charge enable — the PMU owns the host enable rail
    0,     // displayControllerVariant: not probed on this panel
    {}     // viewableInsets: struct defaults; measure the CNC bezel on hardware
};
```

Deliberately left as starting values, each with a hardware check in §4:
`uiScale`, `viewableInsets`, `touch.rawMaxX/rawMaxY`, `swapXY/flipX/flipY` (all
`false` — the mount transform is unknown), and `NO_FLIP`.

### 3.4 New library: `freeink-sdk/libs/hardware/BoardReadPico/`

```text
freeink-sdk/libs/hardware/BoardReadPico/
  library.json                 // name "BoardReadPico", deps BoardConfig + EInkDisplay
  include/BoardReadPico.h
  include/BoardReadPicoPins.h
  src/BoardReadPico.cpp
  src/ReadPicoLgfxConfig.cpp
```

`include/BoardReadPicoPins.h` — plain `#define`s, mirroring
`BoardPaperS3Pins.h:12-38`:

```cpp
#define READPICO_EP_D0  4   … #define READPICO_EP_D15 45      // D0..D15 = GPIO 4..18, 45
#define READPICO_EP_XLE  3    // XLE  (latch enable)
#define READPICO_EP_XSTL 46   // XSTL (start pulse, horizontal)
#define READPICO_EP_XCL  21   // XCL  (pixel clock)
#define READPICO_EP_SPV  47   // SPV  (start pulse, vertical)
#define READPICO_EP_CKV  48   // CKV  (gate clock)
#define READPICO_EP_LGX_DUMMY_PIN   /* see blocker B2 */
// NOTE: `lgfx::pinMode(-1, …)` safety is unverified, and this board's pinout
// claims every S3 GPIO a normal design can use (see blocker B2). Resolve before
// writing this value.
#define READPICO_I2C_SDA 39
#define READPICO_I2C_SCL 40
#define READPICO_IOE_INT 41   // FCA9555 INT# — light-sleep wake only (not an RTC pin)
#define READPICO_TP_INT  43   // CST836U INT# — open-drain, active-low
#define READPICO_ACCEL_INT1 1
#define READPICO_SD_CLK  38
#define READPICO_SD_CMD  42
#define READPICO_SD_D0   44
#define READPICO_BUZZER  2
#define READPICO_IOE_ADDR 0x24
#define READPICO_PMU_ADDR 0x2A
#define READPICO_SY_ADDR  0x62
#define READPICO_TP_ADDR  0x15
#define READPICO_ACCEL_ADDR 0x19
// FCA9555 Port-0 lines (read_pico_board.c bit offsets, app_ioe.c names)
#define READPICO_IOE_MODE     0
#define READPICO_IOE_XOE      1
#define READPICO_IOE_CW_INT   2   // input
#define READPICO_IOE_SY_EN    3
#define READPICO_IOE_VCOM_EN  4
#define READPICO_IOE_PGOOD    5   // input
#define READPICO_IOE_SD_CD    6   // input, active-low
#define READPICO_IOE_TP_RST   7
#define READPICO_IOE_CFG0_EXPECT 0x64
// Panel
// (no VCOM constant: the factory value is read from the PMU and never guessed —
//  see the hardware safety rule in §1.4)
#define READPICO_PCLK_HZ 18000000u       // read_pico_init.c READ_PICO_PCLK_MHZ 18
```

`include/BoardReadPico.h` — the board-support API that `BoardReadPico.cpp` and
the SDK seams call (all in `namespace BoardReadPico`):

| Symbol | Purpose |
|---|---|
| `bool begin()` | Shared I²C init, FCA9555 self-test + Port-0 config `0x64`, MODE/TP_RST preload, touch reset pulse (10 ms low / 50 ms settle), PMU init + identity, accelerometer identity probe, buzzer pin idle LOW |
| `bool ready()` | True when I²C + FCA9555 came up |
| `bool ioeIntAsserted()` / `void clearIoeInt()` | FCA9555 INT# level / read-Input release (`fca9555.h`: INT# is not latched) |
| `bool sdCardPresent()` | FCA9555 **P0.6**, active-low; returns `false` on any I²C read failure |
| `bool touchReset()` | Pulse P0.7 LOW 10 ms, release. **The only way to recover the CST836U from its own deep sleep** |
| `bool touchSleep()` | Put the CST836U into deep sleep (`0xA503`) |
| `uint8_t keyStripHook()` | `InputManager::ButtonHook` implementation: hit-test the three capacitive zones against the latest touch point (split `y > 1300`; centres x = 80/240/400, pitch 160) and return a `1 << BTN_*` mask |
| `bool pmuBattery(uint16_t& mV, uint16_t& socPermille, uint8_t& chargeState)` | `BatteryMonitor::setPmuBatteryHook` implementation |
| `bool pmuTimeGet(uint32_t& unixSec, bool& synced)` / `bool pmuTimeSet(uint32_t unixSec)` | `Rtc::setPmuTimeHooks` implementation |
| `bool pmuPowerOff()` / `bool pmuSoftSleep()` / `bool pmuReportReady()` | Thin wrappers over `read_pico_pmu_power_off()` / `read_pico_pmu_report_sleep()` / `read_pico_pmu_report_ready()` semantics |
| `int pmuVcomMv()` | Read-only `read_pico_pmu_vcom_get()`; returns the factory value in mV, or **`-1`** when it is unavailable or outside `500..2500 mV / multiple of 10`. **No fallback constant** — callers must read `-1` as "do not power the panel" (see the hardware safety rule in §1.4) |
| `bool epdPrepare()`, `bool epdPowerOn()`, `void epdPowerOff()` | The three `freeink::LgfxEpdPowerHooks` bodies (file-static in `ReadPicoLgfxConfig.cpp`, referenced from the config) |

`src/ReadPicoLgfxConfig.cpp` — **exact symbols**:

```cpp
namespace freeink {
const LgfxEpdConfig& readPicoLgfxConfig();   // the ONE symbol the build flag names
}
```

Contents (frozen shape; the only unresolved values are the two `pinOe`/`pinPwr`
dummy entries, `linePadding` and `rotation` — blockers B1/B2):

```cpp
static const LgfxEpdConfig cfg = {
    {READPICO_EP_D0, … , READPICO_EP_D7},              // dataPins[8]  = D0..D7
    READPICO_EP_XSTL,                                  // pinSph  (XSTL) -> i80 CS
    READPICO_EP_SPV,                                   // pinSpv
    READPICO_EP_LGX_DUMMY_PIN,                         // pinOe  (XOE is FCA9555 P0.1, no GPIO)
    READPICO_EP_XLE,                                   // pinLe  (XLE)
    READPICO_EP_XCL,                                   // pinCl  (XCL) -> i80 WR
    READPICO_EP_CKV,                                   // pinCkv
    READPICO_EP_LGX_DUMMY_PIN,                         // pinPwr (EN is FCA9555 P0.3, no GPIO)
    READPICO_PCLK_HZ,                                  // busHz = 18 MHz
    8,                                                 // linePadding — STARTING VALUE, re-tune on hardware
    0,                                                 // rotation — STARTING VALUE; mount transform pending
    {&BoardReadPico::epdPrepare, &BoardReadPico::epdPowerOn, &BoardReadPico::epdPowerOff},
    nullptr, 0,                                        // lutQuality — epd_quality is never selected
    kE0470Gc16, kE0470Gc16Step,                        // lutText  <- epd_text = Full/Half refresh
    kE0470Du, kE0470DuStep,                            // lutFast  <- epd_fast = Fast refresh
    nullptr, 0,                                        // lutFastest — never selected
    {READPICO_EP_D8, READPICO_EP_D9, READPICO_EP_D10, READPICO_EP_D11,
     READPICO_EP_D12, READPICO_EP_D13, READPICO_EP_D14, READPICO_EP_D15},
                                                       // dataPinsHigh[8]: D8..D15 = GPIO 12..18,45
    16                                                 // busWidth = 16
};
```

The board config must `#include <LgfxEpdWaveforms.h>`. The tables live under the
display library's `src/lut/`, which is reachable only from inside that library, so
the public forwarder header is the supported entry point for a board-support
library. The symbols are `kE0470Gc16` / `kE0470Gc16Step`, `kE0470Gl16` /
`kE0470Gl16Step` (landed but deliberately **unwired** — see below) and
`kE0470Du` / `kE0470DuStep`. `dataPinsHigh` is a *braced array*, not a pointer:
the initializer must list eight pin values, and `nullptr, 0` does not compile.

`epdPowerOn()` must reproduce §1.4's verified order — **it replaces the stock
`Bus_EPD::powerControl()` sequence entirely** (`LgfxEpdDriver.cpp:37-50`), so it
owns every line:

0. **VCOM gate, before anything is energised.** `pmuVcomMv()` is read once and must
   return a validated factory value; on `-1` `epdPowerOn()` logs and returns
   immediately, so no rail is raised at all (hardware safety rule, §1.4).
1. `MODE = 1`, `XOE = 0` (FCA9555 P0.0 / P0.1).
2. `SY_EN = 1` (P0.3) and wait for the PMIC digital core.
3. Write VCOM = `BoardReadPico::pmuVcomMv()`, VLDO, delays; set `ON_OFF`; wait
   `PGOOD` (P0.5, 300 ms timeout); raise `VCOM_EN` (P0.4).
4. `XOE = 1`.

`epdPowerOff()`: `XOE = 0`, 1 ms delay, PMIC off, `SY_EN = 0`.
`epdPrepare()`: `pinMode`/idle-level setup for the plain-GPIO lines (SPV, CKV, LE,
XCL, XSTL, data pins), buzzer idle LOW, and the FCA9555 Port-0 preload.

### 3.5 `InputManager` seams

| Symbol | Frozen change | Evidence precedent |
|---|---|---|
| `void InputManager::beginCst836u()` | New private method, `#if FREEINK_DEVICE_READPICO` guarded; called from `beginTouch()` | `beginCst816s()` / `beginFt6336u()` / `beginGslx680()` pattern, `InputManager.cpp:1339-1380` |
| `void InputManager::pollCst836u(unsigned long now)` | New private method; called from `serviceTouch()` under the same guard; returns the strip-key `1 << BTN_*` mask | `pollCst816s()` / `pollFt6336u()` pattern, `InputManager.cpp:1382-1445` |
| `InputManager::prepareForDeepSleep()` | Add a `case BoardConfig::TouchController::Cst836u:` that calls `BoardReadPico::touchSleep()` and clears the contact state | `case …Cst816s:` at `InputManager.cpp:1308-1316` |
| `InputManager::setButtonHook(BoardReadPico::keyStripHook)` | **Reuse** the existing seam (`InputManager.h:233-239`) for the three capacitive zones. No new API | LilyGo uses the same hook for its PCA9535 key (`freeink-sdk/docs/lilygo-t5s3-support.md:73-76`) |
| Post-sleep recovery | After any touch deep sleep, `beginCst836u()` must call `BoardReadPico::touchReset()` and wait for boot before the first I²C read | `cst836u.h`; `read_pico_board.h` |
| Strip-key frame | The key strip lives **outside** the display frame (`y > 1300` vs display `y ≤ 1215`), so the hook must see the **raw** touch point, not the display-mapped one | `main/ui/ui_menu.h`; `main/app/app_loop.c` |

### 3.6 `Rtc` and `BatteryMonitor` seams

Both libs have no hook mechanism today (`Rtc.cpp:114-152`,
`BatteryMonitor.cpp:208-286`), and neither may depend on `BoardReadPico`
(dependency direction: board → libs, not the reverse). The frozen seams mirror the
existing `SDCardManager::setPowerHook` / `InputManager::setButtonHook` pattern:

```cpp
// freeink-sdk/libs/hardware/Rtc/include/Rtc.h
struct PmuTimeHooks {
  bool (*getUnix)(uint32_t& unixSec, bool& synced);
  bool (*setUnix)(uint32_t unixSec);
};
static void setPmuTimeHooks(const PmuTimeHooks& hooks);

// freeink-sdk/libs/hardware/BatteryMonitor/include/BatteryMonitor.h
using PmuBatteryHook = bool (*)(uint16_t& batteryMv, uint16_t& socPermille, uint8_t& chargeState);
static void setPmuBatteryHook(PmuBatteryHook hook);
```

`Rtc::begin()/now()/set()` add a `case BoardConfig::RtcType::Cw32L010Pmu:` that
calls the hooks (`unixSec` → `DateTime`); `BatteryMonitor` adds a
`BoardConfig::GaugeType::Cw32L010Pmu` branch that calls its hook instead of any
register read. `BoardReadPico::begin()` installs both.
With `FREEINK_CAP_RTC` / `FREEINK_BATTERY_I2C_GAUGE` off the libs keep their
existing stub bodies, so the profile stays valid in a capability-off build.

### 3.7 PlatformIO environment

```ini
; --- Read Pico (小纸 Pico, RDP-G01-W) — ESP32-S3 N16R8, E0470A01 684x1216 ------
; Raw-parallel EPD with no on-glass controller: LovyanGFX Panel_EPD/Bus_EPD owns
; the 16-bit i80 bus and the SY7636A/FCA9555 rails come from BoardReadPico.
; USB Serial/JTAG is the console; this board needs no USB MSC, so it extends the
; tuned core (which drops the prebuilt TinyUSB component graph).
;   pio run -e readpico -t upload
[readpico_hardware]
extends = base, firmware_tuned
board = esp32-s3-devkitc1-n16r8
board_build.mcu = esp32s3
board_build.arduino.memory_type = dio_opi
lib_deps =
  ${s3_ble_psram.lib_deps}
  BoardReadPico=symlink://freeink-sdk/libs/hardware/BoardReadPico
  https://github.com/m5stack/M5GFX.git#0.2.20   ; registry has no 0.2.20 — see §3.7
extra_scripts = ${s3_ble_psram.extra_scripts}
custom_sdkconfig =
  ${firmware_tuned.custom_sdkconfig}
  ${s3_ble_controller.custom_sdkconfig}
  ; A1 — keep the IDF console OFF UART0: the board routes CST836U INT# to the S3
  ; default U0TXD (GPIO43) and TF D0 to U0RXD (GPIO44)
  CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
  CONFIG_ESP_CONSOLE_UART_DEFAULT=n
  ; A2 — vendor cache tuning for the row-by-row PSRAM framebuffer streaming
  CONFIG_ESP32S3_DATA_CACHE_64KB=y
  CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
  ; A3 — the vendor's internal-RAM reserve (Panel_EPD wants ~56.5 KiB contiguous
  ; internal DMA for _lut_2pixel, and that allocation fails silently)
  CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
  ; A4 — the vendor's main-task stack (boot runs expander/PMIC bring-up, the
  ; CST836U reset dance and the panel bring-up on that task)
  CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
build_flags =
  ${base.build_flags}
  ${s3_ble_psram.build_flags}
  -DFREEINK_DEVICE_READPICO=1
  -DFREEINK_LGFX_EPD_CONFIG=readPicoLgfxConfig
  -DBOARD_HAS_PSRAM
  -DUSE_BLOCK_DEVICE_INTERFACE=1

[env:readpico]
extends = readpico_hardware
build_flags =
  ${readpico_hardware.build_flags}
  -DCROSSPOINT_VERSION=\"${crosspoint.version}-readpico\"
  -DENABLE_SERIAL_LOG
  -DCROSSPOINT_WAIT_FOR_USB_SERIAL
  -DLOG_LEVEL=2
```

Frozen decisions:

| Item | Decision | Justification |
|---|---|---|
| Section name | `[readpico_hardware]` | Matches `<device>_hardware` (`sticky_hardware`, `metalio_eink4_hardware`, `x4pro_hardware`) |
| Env name | `[env:readpico]` | `pio run -e readpico`; no `_nightly` / `-gh_release` flavor in this round |
| `extends` | `base, firmware_tuned` | No USB MSC is needed (console is USB Serial/JTAG, `sdkconfig.defaults`), so the tuned core's heap reclamation (~32–37 KB, `platformio.ini:126-141`) is free, and the dropped TinyUSB graph (`:122-125`) is harmless. `x4pro`/`x4c`/`murphy_m4`/`metalio_eink4`/`papermono`/`waveshare` keep `base` **only** because they need USB-MSC's TinyUSB graph |
| `board` / `board_build.mcu` | `esp32-s3-devkitc1-n16r8` / `esp32s3` | Same as every other S3 target in `platformio.ini` |
| `board_build.arduino.memory_type` | `dio_opi` | The board has 8 MB **octal** PSRAM (`CONFIG_SPIRAM_MODE_OCT=y`) and the repo's other S3 envs all declare `dio_opi`; `base` already sets `board_build.flash_mode = dio` (`platformio.ini:81`). `qio_opi` is **not** used: the PaperS3's `qio_opi` is specifically M5PaperS3's OPI-PSRAM autodetect requirement (`platformio.sample.ini:204-205`), not a requirement of `Bus_EPD` |
| `-DBOARD_HAS_PSRAM` | Required | ~3.1 MiB of PSRAM allocations plus `FREEINK_FB_PSRAM` |
| `-DFREEINK_LGFX_EPD_CONFIG=readPicoLgfxConfig` | Required | `FREEINK_DRIVER_LGFX_EPD` takes the generic branch `#elif defined(FREEINK_LGFX_EPD_CONFIG)` (`LgfxEpdDriver.cpp:337-342`), so **zero diff** is needed in `LgfxEpdDriver.cpp` for config injection. This is exactly the LilyGo pattern documented at `freeink-sdk/docs/lilygo-t5s3-support.md:50`. **Rejected alternative:** a `PAPERS3`-style auto-select `#elif FREEINK_DEVICE_READPICO` branch in `LgfxEpdDriver.cpp` — it adds a device-name branch to a shared driver for no benefit |
| `-DUSE_BLOCK_DEVICE_INTERFACE=1` | Required | `SdmmcBlockDevice` needs SdFat built with it (`SDCardManager/src/SdmmcBlockDevice.h:18-20`); every `FREEINK_SD_SDMMC` env sets it |
| `https://github.com/m5stack/M5GFX.git#0.2.20` in `lib_deps` | Required | `LgfxEpdDriver.cpp:8` includes `<M5GFX.h>` to pull LovyanGFX. **The git tag is pinned, not the registry form**: the PlatformIO Registry has no `0.2.20` for `m5stack/M5GFX` (only 0.1.17 / 0.2.27 / 0.2.28 / 0.2.29), so `m5stack/M5GFX @ 0.2.20` fails with `UnknownPackageError`. The tag is preferred over bumping to 0.2.29 because every LUT/pin/allocation conclusion here was verified against the 0.2.20 tree — a bump would silently invalidate that evidence. Bumping requires re-verifying `Panel_EPD.{hpp,cpp}` and `Bus_EPD.{h,c}` at the new revision first |
| Console on USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, UART console off) | **Required (A1)** | The ESP32-S3 default UART0 pins are GPIO43 (U0TXD) / GPIO44 (U0RXD). This board uses GPIO43 for the CST836U INT# (`read_pico_board.h`: "CST836U INT on TXD0 / GPIO43") and GPIO44 for TF card D0. Leaving the IDF console on UART0 claims both pads; the vendor `sdkconfig.defaults` sets USB Serial/JTAG for exactly this reason |
| 64 KB data cache / 64 B lines | **Required (A2)** | Vendor tuning for the reader streaming the framebuffer out of PSRAM row by row. Scoped to this env — other S3 targets keep 32 KB |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` | **Required (A3)** | Vendor reserve. `Panel_EPD::_lut_2pixel` needs ~56.5 KiB of contiguous internal DMA RAM and `init_intenal()` fails silently |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` | **Required (A4)** | Vendor value. The boot path runs expander/PMIC bring-up, the CST836U reset dance and the panel bring-up on the main task; the eego A4 precedent needed 16 KB once a network step landed on it |
| `CONFIG_COMPILER_OPTIMIZATION_PERF` | **Deliberately not set** | The vendor builds with `PERF`; this port stays on `-Os` because the image already occupies 92.3 % of the 6.25 MB OTA slot (§4 row 6). Raise it only together with a size measurement — a blind `-O2` risks overflowing the only app slot this target has |
| Buzzer (`CROSSPOINT_CAP_SOUND_FEEDBACK`, `AudioManager`, `assets/sounds/*.wav`) | **Deliberately not set** | `platformio.ini:612-613` states a board opts into `[sound_feedback_hardware]` "only after its speaker path and level calibration have passed hardware acceptance". This port has had no hardware acceptance, so opting in now would contradict the project's own rule. The three lines needed are the `[sound_feedback_hardware]` body: `board_build.embed_files`, `-DCROSSPOINT_CAP_SOUND_FEEDBACK=1`, and the `AudioManager` lib_dep |
| Pickup wake (SC7A20H INT1 on GPIO1) | **Deliberately not set** | The vendor's demo wakes on "key or pickup". Arming GPIO1 needs `CTRL3` INT1_CFG plus AOI1/HPIS1 configured in the SDK `Imu` library, whose register semantics are not covered by the current `Imu` API. Writing unverifiable accelerometer interrupt configuration is exactly the "do not invent register semantics" case, so it is left as a feature request rather than coded blind |
| `s3_ble_psram` | Inherited | Consistency with the other S3 targets (`device-variants.md:68-74`) |
| `-DFREEINK_CAP_USB_MSC` | **Not set** | The reference console is USB Serial/JTAG and no MSC workflow is in scope |
| 120 MHz flash/PSRAM (`CONFIG_ESPTOOLPY_FLASHFREQ_120M`, `CONFIG_SPIRAM_SPEED_120M`, HPM patch) | **Not set** | See §2.7 |

### 3.8 Partition / flash plan

The repo's [`partitions.csv`](../../partitions.csv) and the board's factory
`partitions_16M.csv` are **incompatible**:

| | CrossMux `partitions.csv` | Board factory `partitions_16M.csv` |
|---|---|---|
| `nvs` | 0x9000, 0x5000 (20 KB) | 0x9000, 0x5000 (20 KB) |
| `phy_init` | — | 0xE000, 0x1000 (4 KB) |
| `otadata` | 0xE000, 0x2000 (8 KB) | — |
| app slot(s) | `app0` 0x10000 **0x640000 (6.25 MB)** + `app1` 0x650000 6.25 MB | `factory` 0x10000 **0x200000 (2 MB)** |
| data | `spiffs` 0xc90000, 0x360000 (3.375 MB); `coredump` 0xFF0000, 0x10000 | `spiffs` 5 MB |

Consequences:

- The two tables overlap at **0xE000** (`otadata` versus `phy_init`) and the
  factory app slot is 2 MB against CrossMux's 6.25 MB. A CrossMux `readpico`
  image cannot be placed into the factory layout.
- **First installation therefore requires a full layout install**, not an
  app-only write: bootloader at `0x0`, `partitions.bin` at `0x8000`,
  `boot_app0.bin` at `0xe000`, application at `0x10000`. This is the same
  constraint Metalio documents (`metalio-eink4.md:19-25`); derive every offset and
  size from the built image, never from another board's example.
- Per `port-device-bsp/SKILL.md:134-138`: **before the first overwrite of factory
  firmware, record the user's confirmation of an existing backup or prepare and
  verify a recoverable backup** (a full 16 MiB read), and keep it outside the
  device. Flashing is authorized only inside the task that performs it.
- The 6.25 MB OTA slot is the headroom to watch: the readpico image adds M5GFX on
  top of the shared CrossMux feature set. **No size was measured in this round.**

### 3.9 Out of scope (explicitly)

- **Nightly / OTA / Web release enrollment is out of scope and remains pending.**
  No `readpico_nightly` env, no `scripts/nightly_targets.py` mapping, no manifest,
  no index or board-tag work. Per `port-device-bsp/SKILL.md:237-249`, release
  enrollment is a separate task that begins by inspecting
  `scripts/nightly_targets.py` and its consumers.
- No simulator target (`simulator_readpico`) is added.
- No CrossMux source, HAL, README or other engineering doc is modified by the
  interface-freeze task; registering this document in
  [`docs/engineering/index.md`](index.md) and updating the two README language
  versions (a `port-device-bsp` handoff requirement, `SKILL.md:183-187`) are
  separate, deliberately deferred edits.
- No PSRAM/flash timing change, no partition-table change, no SDK driver
  behaviour change beyond §2.3.

## 4. Verification status

Statuses: **passed / failed / pending / not applicable**. Every build, CI, flash
and on-hardware check is **pending** because the user explicitly asked not to
compile anything in this round. No current, refresh time, heap or PSRAM
measurement was taken, and none is asserted anywhere in this document.

| # | Check | Target & revision | Status | Evidence / reason |
|---|---|---|---|---|
| 1 | Upstream hardware evidence collected and read | `read_pico_firmware` `main`; `dot_web_docs` `zh-Hans-CN/read_0/*` | **passed** | Every §1 row cites its file; see also the source list in §5.1 |
| 2 | Repository/SDK interface inventory | CrossMux `80c67543`; SDK `094976e1` | **passed** | `BoardConfig.h`, `LgfxEpdConfig.h`, `LgfxEpdDriver.{h,cpp}`, `FreeInkDisplay.{h,cpp}`, `InputManager.{h,cpp}`, `Imu.cpp`, `Rtc.cpp`, `BatteryMonitor.cpp`, `SdmmcBlockDevice.{h,cpp}`, `platformio.ini`, `partitions.csv` read and cited |
| 3 | LovyanGFX pin/LUT/allocation semantics read | pinned `m5stack/M5GFX @ 0.2.20` (`Bus_EPD.{h,cpp}`, `Panel_EPD.{hpp,cpp}`) | **passed** (pinned `0.2.20` tree) | `Bus_EPD.h` `config_t`; `Panel_EPD.cpp:228`, `:232`, `:235`, `:238-240`, `:276-283`; `LUT_MAKE` macro. Verified against the **0.2.20** git tag (lines identical to `master`). B3's source-verification half is closed; the on-hardware A/B half remains |
| 4 | Frozen interface is internally consistent | This document, §3 | **passed** | Names cross-checked against the SDK enums/structs they extend |
| 5 | `[env:readpico]` resolves | `[env:readpico]` | **passed** | `pio project config` and `--lint` both exit 0; `-e` is not a valid option in PIO 6.1.19 (exit 2) — use `--json-output` |
| 6 | `pio run -e readpico` compiles | CrossMux `80c67543` + SDK `094976e1` + the port diff | **passed** | 2026-09-25: `SUCCESS`, `firmware.bin` 6,046,672 B, RAM 23.3%, Flash 92.3% (6,046,171 / 6,553,600). Four blockers were found and fixed first (see the handoff note §8) |
| 7 | Existing S3 regression build | `eego_a4` attempted; `metalio_eink4`, `waveshare_epaper_397`, `murphy_m4`, `x4pro`, `sticky`, `papermono`, `x4c` | **pending** | `BoardConfig.h`, `LgfxEpdConfig.h` and `LgfxEpdDriver.cpp` are shared; `eego_a4` failed on a dead local git proxy (`127.0.0.1:7890`), not on code — rerun with the proxy bypass |
| 8 | Shared X3/X4 image regression build | `default` | **passed** | 2026-09-25: `SUCCESS`, RAM 20.0%, Flash 97.8% (6,409,277 / 6,553,600). Note the C3 image has only ~144 KB of headroom |
| 9 | `pio check` (cppcheck) on the new files | new SDK board library + platformio env | **pending** | Not run |
| 10 | Hosted CI | CrossMux CI workflow | **pending** | Not run; `bin/ci-check` enumerates seven envs and does not include `readpico` |
| 11 | Repository script/unit tests | `scripts/tests` | **pending** | Not run |
| 12 | Third-party licensing review of imported data | `e0470_epaper_waveform` waveforms (Apache-2.0) and the epdiy-derived timing (LGPL-3.0-or-later) | **pending** | `read_pico_firmware/README.md` (Acknowledgments) records both licences; no review performed. This gates any code that *imports* the tables rather than re-deriving them |
| 13 | Flash + esptool hash verification | to be flashed `readpico` image | **pending** | Not flashed. Requires a recorded full-chip backup first (§3.8) |
| 14 | First-boot identity in the serial log | flashed `readpico`, USB Serial/JTAG @115200 | **pending** | No hardware access in this round |
| 15 | Panel drive A/B against the reference firmware | same unit: GC16 / GL16 / DU / gray8 | **pending** | No hardware. Depends on blocker B3 |
| 16 | Panel orientation, bezel insets, `linePadding` | flashed `readpico` | **pending** | `rotation = 0`, `NO_FLIP`, `uiScale = 1.2f`, `viewableInsets = {}` and `linePadding = 8` are unmeasured starting values |
| 17 | Touch: 2 points, coordinate frame, strip-key zones | flashed `readpico` | **pending** | Raw point dump required; `rawMaxX/Y` and the strip mapping are unconfirmed |
| 18 | Touch recovery after chip deep sleep via RST | flashed `readpico` | **pending** | The rule is verified in source; the recovery itself is a hardware behaviour |
| 19 | Accelerometer identity, at-rest and tap/orientation | flashed `readpico` | **pending** | `WHO_AM_I 0x11` / at-rest values come from the vendor firmware, not from this unit |
| 20 | SD mount, read/write, EPUB open, page turn, progress persists | flashed `readpico` + FAT32 card | **pending** | No hardware |
| 21 | SD absent / unreadable card handling | flashed `readpico` | **pending** | No hardware |
| 22 | Buzzer tone | flashed `readpico` | **pending** | No hardware |
| 23 | PMU: battery mV / SoC / charge state / LED / RTC / alarm / power-off | flashed `readpico` | **pending** | No hardware. Depends on the §3.6 seams landing |
| 24 | Light-sleep wake on FCA9555 INT# (GPIO41) and pickup (GPIO1) | flashed `readpico` | **pending** | No hardware |
| 25 | ≥ 3 power-off → boot cycles, display/input/storage recovery | flashed `readpico` | **pending** | No hardware |
| 26 | Deepsleep/off wake via PMU key / AC-in / alarm | flashed `readpico` | **pending** | No hardware. Requires the new PMU wake seam (blocker B10) |
| 27 | Internal heap + PSRAM watermarks before/after init and repeated reading | flashed `readpico` | **pending** | No hardware; the §2.6 numbers are arithmetic, not measurements |
| 28 | `_lut_2pixel` internal-DMA allocation actually succeeds (≈ 56.5 KiB with the GC16/DU tables wired) | flashed `readpico` | **pending** | `Panel_EPD::init_intenal()` returns false if it fails (`Panel_EPD.cpp:242-250`) — an OOM here is a silent dead panel |
| 29 | Current consumption (light sleep / soft sleep / off) | flashed `readpico` + instrument | **pending** | Never measured, and no value is claimed |
| 30 | Simulator target | — | **not applicable** | No simulator support is requested or planned for this target (§3.9) |
| 31 | Nightly / OTA / Web release enrollment | `scripts/nightly_targets.py` and consumers | **not applicable** | Explicitly out of scope for this port (§3.9); becomes a separate pending task only if the user requests it |
| 32 | `docs/engineering/index.md` registration + both README language versions | — | **not applicable** | Deferred by the "exactly one new file" scope of this round (§3.9); tracked as blocker B13 |

## 5. Remaining blockers and next actions

### 5.1 Hardware facts and sources (including unresolved assumptions)

Sources actually fetched and used in this round:

- `MindReset/read_pico_firmware` @ `main`:
  `README.md`; `partitions_16M.csv`; `sdkconfig.defaults`;
  `components/read_pico/{README.md,read_pico_board.c,read_pico_init.c,read_pico_sd.c,
  read_pico_buzzer.c}`;
  `components/read_pico/include/{read_pico_board.h,read_pico_epd_timing.h,
  read_pico_init.h,read_pico_sd.h}`;
  `components/read_pico_pmu/{README.md,include/read_pico_pmu.h,
  include/read_pico_pmu_protocol.h}`;
  `components/cst836u/{include/cst836u.h,cst836u.c}`;
  `components/sc7a20h/include/sc7a20h.h`;
  `components/fca9555/include/fca9555.h`;
  `components/sy7636a/include/sy7636a.h`;
  `components/e0470_epaper_waveform/{README.md,include/e0470_epaper_waveform.h,
  e0470_epaper_waveform.c,waveforms/gc16.h}`;
  `components/epdiy/src/{epd_board.h,epd_display.h,displays.c,epd_internals.h}`;
  `components/epdiy/include/epd_waveform.h`;
  `main/{sleep.c,app/app.h,app/app_config.h,app/app_loop.c,ui/ui_kit.h,
  ui/ui_menu.h,apps/app_ioe.c,apps/app_sleep.c,apps/app_power.c,apps/app_key.c}`.
  Paths that appeared only in a directory/tree listing (not read):
  `components/read_pico/{Kconfig,buzzer_1bit.c,read_pico_epd_timing.c,
  read_pico_flash_hpm.c}`, `components/read_pico_pmu/docs/*`.
- `MindReset/dot_web_docs` @ `main`: `zh-Hans-CN/read_0/index.mdx`,
  `zh-Hans-CN/read_0/start.mdx`, `zh-Hans-CN/read_0/firmware.mdx`.
- `lovyan03/LovyanGFX` @ `master`:
  `src/lgfx/v1/platforms/esp32/Bus_EPD.h`, `Bus_EPD.cpp`, `Panel_EPD.hpp`,
  `Panel_EPD.cpp`, `common.hpp`.

Unresolved assumptions carried forward (each is a blocker or a §4 pending row):

- Panel mount transform (`rotation`, `NO_FLIP`), bezel insets and `uiScale`.
- The value to pass for `pinOe`/`pinPwr` — whether `lgfx::pinMode(-1, …)` is
  tolerated, and if not, which pin can host the dummy (B2).
- The XSTL-as-i80-CS convention versus epdiy's XSTL-as-DE convention.
- The touch plane's full raw range and the exact strip-key mapping.
- Whether any other I²C device exists on the bus.
- Whether the PMU exposes a VBUS-present signal beyond `charge_state`.
- The `_lut_2pixel` / PSRAM budgets actually fitting at run time.
- The pinned M5GFX `0.2.20` LovyanGFX revision's LUT semantics.

Sources **not** reachable in this round (so nothing depends on them):

- `raw.githubusercontent.com` timed out for every attempt (as the task warned);
  everything upstream was fetched through the GitHub contents API.
- The GitHub contents API hit its unauthenticated rate limit near the end of the
  session, so `components/epdiy/src/output_lcd/*`,
  `components/read_pico_pmu/docs/llms-full_*.md`,
  `docs/pmu_registers_*.json`, `components/read_pico/read_pico_epd_timing.c`,
  `main/app_main.c`, `main/display.c`, `main/apps/app_registry.c` and
  `sc7a20h_lab.h` were **not** read. None of the frozen names in §3 depend on
  them; they are listed as follow-up reading for the implementation tasks.
- The local `.pio/libdeps` tree contains no M5GFX copy, so no pinned
  LovyanGFX source could be inspected locally (blocker B3).

### 5.2 Selected route and changes (summary)

| # | Change | File |
|---|---|---|
| 1 | Device flag, MCU/driver/capability derivation, `Board`/`InputStyle`/`TouchController`/`ImuType`/`RtcType`/`GaugeType` values, `READ_PICO` profile, `READ_PICO_AUDIO`, `DEFAULT_DEVICE`, `selectDevice()`, `isReadPico()`, `MAX_FRAMEBUFFER_BYTES` term, `static_assert`s | `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h` |
| 2 | `dataPinsHigh[8]` + `busWidth` (appended) | `freeink-sdk/libs/display/FreeInkDisplay/include/LgfxEpdConfig.h` |
| 3 | Honour `busWidth` / `dataPinsHigh` (4 lines) | `freeink-sdk/libs/display/FreeInkDisplay/src/driver/LgfxEpdDriver.cpp` (`FreeInkLgfxEpd::setup`) |
| 4 | New board-support library | `freeink-sdk/libs/hardware/BoardReadPico/**` |
| 5 | CST836U touch backend + strip-key hook + deep-sleep recovery | `freeink-sdk/libs/hardware/InputManager/{include/InputManager.h,src/InputManager.cpp}` |
| 6 | SC7A20H IMU backend (accel-only) | `freeink-sdk/libs/hardware/Imu/src/Imu.cpp` |
| 7 | PMU time hooks + `Cw32L010Pmu` backend | `freeink-sdk/libs/hardware/Rtc/{include/Rtc.h,src/Rtc.cpp}` |
| 8 | PMU battery hook + `Cw32L010Pmu` backend | `freeink-sdk/libs/hardware/BatteryMonitor/{include/BatteryMonitor.h,src/BatteryMonitor.cpp}` |
| 9 | Build environment | `platformio.ini` (`[readpico_hardware]`, `[env:readpico]`) |
| 10 | HAL/app integration (sleep + wake policy, clock fallback) | CrossMux `lib/hal/*`, `src/main.cpp` — task 3 |

### 5.3 Blockers and their concrete next actions

| # | Blocker | Concrete next action |
|---|---|---|
| **B1** | Panel mount transform, bezel insets, `uiScale`, `linePadding` are unknown; the frozen profile uses placeholders | Build and flash once the layout is installed, then render a known asymmetric test pattern and read the `rotation`/`NO_FLIP` combination off the glass; measure the CNC bezel overlap for `viewableInsets`; only then tune `linePadding` against ghosting |
| **B2** | `pinOe` and `pinPwr` have no GPIO on this board, but LovyanGFX calls `pinMode()` on both at `Bus_EPD::init()`; passing −1 is unverified, and the pinout claims every usable S3 GPIO | First, confirm `lgfx::pinMode(-1, …)` behaviour in the pinned M5GFX; if −1 is safe, pass `PIN_UNASSIGNED` and the problem disappears. If it is not safe, find a pin with no board function from the schematic — **do not** assume one exists: GPIO26–37 are flash/octal PSRAM, GPIO19/20 are the native USB pair, GPIO1–18/21/38–48 are all claimed (§1.1), and the only unaccounted number is **GPIO0**, which is a boot strapping pin. Freeze the result as `READPICO_EP_LGX_DUMMY_PIN`, remembering `pin_pwr` also doubles as the i80 DC line |
| **B3** | The LUT conversion is unproven: epdiy is a 16 × 16 (from → to) matrix (64 B/frame), LovyanGFX is a 16-entry per-level vector (4 B/frame) | Diff the pinned `m5stack/M5GFX @ 0.2.20` vendored LovyanGFX against the `master` sources cited in §2.5 (`Panel_EPD.cpp:276-283`, `LUT_MAKE`, `Bus_EPD.h` `config_t`). Then implement the explicit conversion and A/B GC16/GL16/DU/gray8 against the reference firmware on the same unit before trusting any waveform |
| **B4** | The Lgfx path cannot express the vendor line timing (L_SL/L_BL/L_EL, CKV high width, the 11 090 µs frame target); XSTL is driven as an i80 CS rather than a DE signal | First try `busHz = 18 MHz` + `linePadding` tuning and judge on hardware. If contrast/ghosting shows the line timing matters — or if the panel rejects the CS-form start pulse — design an SDK-side extension of `Bus_EPD`/`Panel_EPD` (a config field or virtual) instead of a board-side workaround. Record the outcome here |
| **B5** | Touch raw range and the strip-key mapping are unconfirmed; `rawMaxX = 683` / `rawMaxY = 1215` is an assumption | Flash and dump raw points: the display corners and each of the three key zones. Confirm the "display y ≤ 1215 / strip y > 1300" split and the x = 80/240/400 centres on this unit, then pin `rawMax*` and the hook geometry |
| **B6** | `_lut_2pixel` consumes ≈ 56.5 KiB (**≈ 65 KiB** if GL16 is wired too) of **internal DMA** RAM with the landed tables, and `Panel_EPD::init_intenal()` fails silently to the user if that allocation fails | Log internal heap before/after display `begin()`. Note a null LUT pointer does **not** yield 0 steps (`Panel_EPD.cpp:178-197` substitutes a built-in), so the only real levers are which mode carries which table and how many tables are wired. Do **not** patch M5GFX: the upstream code over-allocates 2× (`Panel_EPD.cpp:228`), but it is a registry dependency |
| **B7** | ~3.1 MiB of PSRAM is requested by the display stack alone (1.59 MiB `_step_framebuf` + 406 KiB `_buf` + 812 KiB canvas + 203 KiB planes + 102 KiB framebuffer) | Confirm 8 MB octal PSRAM is detected, and log free/largest PSRAM block before and after display `begin()`. Treat the numbers as arithmetic from the cited allocation expressions until measured |
| **B8** | The 120 MHz flash/PSRAM configuration (and with it the vendor's PSRAM bandwidth) is deliberately not adopted (§2.7), so refreshes will be slower than the reference firmware | After the panel works at default timing, measure refresh time per mode, then evaluate the 120 MHz configuration separately with its documented temperature risk and the `firmware_tuned` incompatibility in mind |
| **B9** | The CST836U INT# (GPIO43) is not an RTC-capable S3 pin, and the reference firmware does not use it as a host wake source at all (light sleep wakes on GPIO41 and optionally GPIO1) | Decide the CrossMux wake set explicitly: reuse the reference behaviour (IOE INT# + optional pickup + timer) for light sleep. Do not arm GPIO43 as a deep-sleep EXT source — it cannot be one |
| **B10** | "Deep sleep" on this board is a PMU-driven host shutdown, not `esp_deep_sleep_start()`; the real wake sources (PMU key, AC-in, RTC alarm) have no SDK seam | Add a board-owned seam for the PMU power handoff (`pmuReportReady` / `pmuSoftSleep` / `pmuPowerOff`, §3.4) plus a documented wake-reason path, and record which of key / AC-in / alarm CrossMux exposes. Until then, treat sleep as light sleep only |
| **B11** | SD card detect (FCA9555 P0.6) has no SDK seam, and `SDCardManager` has no card-detect hook (only `setPowerHook`, `SDCardManager.h:79-80`) | Round 1: mount by attempt and expose `BoardReadPico::sdCardPresent()` for UI hints only. If a real CD-driven flow is required, add the hook to `SDCardManager` explicitly rather than reading the expander from an activity (golden rule #4) |
| **B12** | ✅ **Closed for safety (2026-09-25).** VCOM is factory-written and must not be changed (vendor handover): a value that does not match the glass damages it permanently | `BoardReadPico::pmuVcomMv()` is read-only and returns `-1` on any failure or range violation; there is **no fallback constant**; `epdPowerOn()` returns before energising any rail when it sees `-1`. Nothing in the port calls the PMU's VCOM setter (factory page only). Remaining hardware question: read and record what this unit's factory value actually is, and confirm it is stable across boots |
| **B13** | This round may create exactly one file, so `docs/engineering/index.md` and the two README language versions are not updated | Schedule the index registration and both README edits as part of the implementation tasks, per `port-device-bsp/SKILL.md:183-187` |
| **B14** | Nightly / OTA / Web enrollment | Out of scope and pending (§3.9). Only if the user requests it: read `firmware-release.md`, then inspect `scripts/nightly_targets.py` and every consumer before choosing a rollout order |
| **B15** | The vendor documents a post-wake USB enumeration failure on this board (S3 native USB): after waking from sleep the device may not be recognised. Their own troubleshooting order is (1) try a USB Type-A data cable, (2) sleep and wake once more, (3) reboot the board and retry | Operational, not a firmware defect — record it in the user-facing notes rather than "fixing" it. Re-test it once the port's sleep path runs on hardware, since this port uses light sleep plus a PMU-driven host shutdown rather than the vendor's `esp_deep_sleep_start()` |

### 5.4 Items that only compiling or real hardware can settle

**Only a compile can settle**

- Whether the frozen `BoardConfig.h` edits compose: the coherence `#if`, the
  `FREEINK_MCU_S3` computation, the `static_assert`s, and the positional
  `BoardProfile` / `TouchConfig` / `AudioConfig` initializers.
- Whether the appended `LgfxEpdConfig` members keep `BoardT5S3` and
  `BoardPaperS3` compiling with their existing brace initializers, and whether
  the shared-header regression envs all still build.
- The final image size against the 6.25 MB OTA slot, and the 2 MB factory slot's
  unsuitability (§3.8).
- The actual `pio project config -e readpico` resolution (`lib_deps` inheritance,
  `custom_sdkconfig` merge, memory type).

**Only real hardware can settle**

- Panel drive quality, orientation, ghosting, refresh time per mode, and the
  whole LUT question (B3).
- Whether the E0470A01 accepts LovyanGFX's XSTL-as-CS start pulse and i80 line
  timing (B4).
- The dummy-GPIO choice and its side effects (B2).
- Touch coordinate frame, strip-key geometry, two-point behaviour, and recovery
  from the chip's own deep sleep (B5, B9).
- Accelerometer identity and orientation on this unit; SD mount and storage
  behaviour; buzzer loudness; PMU battery/charge/LED/RTC/alarm/power control;
  the real wake sources (B10).
- Internal-heap, PSRAM and `_lut_2pixel` watermarks (B6, B7).
- Sleep/off current, which no source in this document quantifies (B8, row 29).

## 6. Display backend switched to epdiy's LCD path (B4 closed)

This section supersedes the LgfxEpd/i80 route described in §2.4 and §3.7. Read Pico
now builds `FREEINK_DRIVER_EPDIY_LCD`; the LgfxEpd path is no longer linked on this
target. Other targets are untouched (`FREEINK_DRIVER_LGFX_EPD` still drives LilyGo
T5 S3 and M5Stack PaperS3).

### 6.1 What B4 actually was

The panel's start line is not something the i80 peripheral can express. The
reference firmware drives the panel as an LCD: `XLE ← HSYNC`, `XCL ← PCLK`,
`XSTL ← DE`, with CKV on RMT
(`read_pico_firmware/components/epdiy/src/output_lcd/lcd_driver.c:423-426`).
LovyanGFX's `Bus_EPD` is an **i80** bus — `esp_lcd_new_i80_bus` with
`wr_gpio_num = pin_cl` and `cs_gpio_num = pin_sph`
(`M5GFX/src/lgfx/v1/platforms/esp32/Bus_EPD.cpp:124-156`) — and i80 has no DE at
all. The pad that carries XSTL can therefore only be the i80 CS, asserted across the
whole burst instead of per line's data window, while XLE/CKV/SPV are plain GPIOs
toggled from the transfer ISR (`Bus_EPD.cpp:37-51, 105-109`). That is the
花屏/错位/撕裂 mechanism: architectural, not a tunable.

The LUT model also paid for it: M5GFX's tables are one vector per frame indexed by
destination only, so the vendor's 2-D `data[frame][to][from/4]` had to be projected —
94.1 % of GC16 actions and only **85.6 %** of DU (`E0470Waveforms.h:40-55`). epdiy's
native tables remove that loss.

### 6.2 What was vendored

New PlatformIO library `freeink-sdk/libs/display/EpdiyLcd/`, listed only in
`[readpico_hardware] lib_deps`, so no other env sees it:

- `src/epdiy/**` — the vendor's trimmed epdiy v2.0.0 fork, **copied verbatim**
  (30 files: `lcd_driver.c`, `render_lcd.c`, `render.c`, `highlevel.c`, `lut.c`,
  `lut.S`, `diff.S`, `rmt_compat.c`, `epd_board.c`, `displays.c`, …). `font.c` is
  excluded (it needs `miniz`, which upstream never declares).
- `src/e0470/**` — the vendor's waveform component (`e0470_epaper_waveform.c`,
  `e0470_waveform_trim.c`, `waveforms/*.h`).
- `include/EpdiyLcd.h`, `src/EpdiyLcd.cpp` — the SDK-side wrapper: supplies the three
  things epdiy deliberately leaves to the board (an `EpdBoardDefinition`, the
  bus/scan timing, the waveform) and feeds frames in.

**Licence.** `src/epdiy/**` is **LGPL-3.0-or-later** (`src/epdiy/LICENSE:19-21`),
which is copyleft and therefore *not* MIT like the rest of CrossMux. The waveform
component is Apache-2.0. The vendored tree sits in its own library directory and is
linked into the readpico image only, so no other target's binary contains it; the
readpico firmware as a whole does carry the LGPL obligation. Decide deliberately
before shipping.

**IDF 5.5.2 compatibility — verified, not assumed.** Every IDF-6-only construct in
the LCD path is inside `#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6,0,0)` with a
5.x `#else`. The symbols the 5.5.2 branches need were checked present under
`framework-arduinoespressif32-libs/esp32s3/include/`: `PERIPH_RCC_ATOMIC`
(`esp_private/periph_ctrl.h:51`), `lcd_periph_rgb_signals` (`soc/lcd_periph.h:45`),
the 4-argument `lcd_hal_cal_pclk_freq` (`hal/lcd_hal.h:47` — so
`#ifdef LCD_HAL_PCLK_FLAG_ALLOW_EQUAL_SYSCLK` takes the `#else`),
`lcd_ll_set_dma_read_stride` / `lcd_ll_set_data_wire_width` (`hal/lcd_ll.h:385,406`),
`rmt_mem_t` (`driver/rmt_types_legacy.h:49`), `LCD_H_ENABLE_IDX 150`
(`soc/gpio_sig_map.h:270`). `lcd_driver.c` and `render_lcd.c` compiled with no
errors; the only diagnostics are two `-Wdeprecated-declarations` notes for
`gdma_new_channel` / `gdma_set_transfer_ability`, which is simply what the 5.x
branch uses.

### 6.3 Wiring

- `platformio.ini [readpico_hardware]`: M5GFX **removed** from `lib_deps` (it would
  otherwise link `Bus_EPD`/`Panel_EPD` for a board that no longer uses them);
  `EpdiyLcd=symlink://…` added; `-DFREEINK_EPDIY_LCD_CONFIG=readPicoEpdiyConfig`
  replaces `-DFREEINK_LGFX_EPD_CONFIG=readPicoLgfxConfig`; four `-I` flags give
  epdiy its own include roots (upstream ships them as an ESP-IDF component's
  `INCLUDE_DIRS`, and PlatformIO ignores that wrapper).
- `BoardConfig.h`: `FREEINK_DRIVER_EPDIY_LCD` derived from `FREEINK_DEVICE_READPICO`;
  READPICO removed from the `FREEINK_DRIVER_LGFX_EPD` condition.
- `FreeInkDisplay.cpp`: one new `selectDriver()` branch plus a guarded include.
- `EpdiyLcdDriver.{h,cpp}` (FreeInkDisplay): the `PanelDriver` adapter.
- `BoardReadPico/src/ReadPicoEpdiyConfig.cpp`: pins, the scan-timing solve carried
  over from the vendor's `read_pico_epd_timing.c` (Apache-2.0), and the power hooks —
  reusing `BoardReadPico::epdPrepare/epdPowerOn/epdPowerOff`, so the VCOM gate stays
  ahead of every rail. At 18 MHz the FULL profile solves to
  `L_SL=6, L_BL=15, L_DL=152, L_EL=115, CKV=134 (0.1 µs)`, prefill 32 lines.

### 6.4 Measured

`env:readpico` builds at RAM 32.6 % / Flash 92.0 %. On hardware the device boots, the
panel comes up at 1216x684, and the VCOM gate reads the PMU's factory value before
any rail rises:

```
[DISP] panel 1216x684 (152 bytes/row, 103968-byte framebuffer); PSRAM free=6253916
[RDP] panel VCOM 1580 mV (PMU factory value)
[RDP] EPD rails on (SY7636A), XOE raised
```

A page refresh is ~400 ms, matching the vendor's 36-phase GC16 figure.

### 6.5 Two defects found and fixed while landing it

1. **Null-pointer panic at boot.** `epdiyLcdBegin()` called `epd_width()` *before*
   `epd_init()`. `epd_init()` is what assigns the display table (`epdiy.c:487-491`),
   so `epd_get_display()` was still NULL. Symbolized backtrace:
   `epd_width → EpdiyLcd.cpp:162`. The geometry check now runs after `epd_init()`.
2. **X3/X4 build break.** `EpdiyLcdDriver.h` included `<EpdiyLcd.h>` unconditionally,
   but that library is only in readpico's `lib_deps`, and PlatformIO compiles every
   source of a library that *any* env uses — so `default` died with
   `fatal error: EpdiyLcd.h: No such file`. The `FREEINK_DRIVER_EPDIY_LCD` guard now
   lives *inside* the header, not only at the include site.

### 6.6 Anti-aliasing rendered a negative

`PanelDriver::displayGray()` was left at its base implementation, which pushes the
`fb` it is handed (`PanelDriver.h:138-142`). At commit time that buffer is **not the
page**: the host clears to `0x00`, renders the LSB/MSB selector plane, copies it out
and then calls `displayGrayBuffer()` (`ReaderUtils.h:162-186`,
`EpubReaderActivity.cpp:2185-2219`). The plane is bit-complementary to the page
(background 0 vs paper 1), so the panel showed the page's negative — 整屏反色.
`LgfxEpdDriver.cpp:187-201` had already written down the same trap and its
conclusion.

Fix: `EpdiyLcd` keeps a 1 bpp base image from the last `epdiyLcdDraw()` and gains
`epdiyLcdDrawGray()`, which rebuilds the page from that base and only re-levels the
pixels the selector planes pick (`kGrayLight=0xAA → level 5`, `kGrayDark=0x55 →
level 10`, aligned with LgfxEpdDriver's canvas values). `EpdiyLcdDriver` implements
`copyGrayscaleLsb/Msb` and `displayGray` — the latter deliberately ignores `fb` — and
pushes with the *base frame's* profile so the overlay does not re-drive the whole
panel.

Note on `kBlackIsOne` (`EpdiyLcdDriver.cpp`): the facade's contract is "a set bit is
white" (`FreeInkDisplay.cpp:263-264`), while the driver tells EpdiyLcd "a set bit is
black". The two are reconciled inside the expansion table, and the black/white page is
*correct on hardware* with `true`, which means epdiy level 0 renders white on this
glass. It is kept as-is deliberately; a source-only reading would flip it and invert
the path that currently works.

That is not a hypothetical: it was flipped to `false` for one round on the strength of
an "everything is inverted" report, and the screen came out inverted; reverting to
`true` restored it. The lesson is recorded next to the constant — **judge polarity only
after a clean full-screen refresh**, because the "old image residue" that prompted the
report makes brightness judgments unreliable. The boot-time global refresh (§6.6 above)
is what provides that baseline.

### 6.7 Touch coordinate frame — derived from the vendor's UI frame

**Final profile** (`BoardConfig.h`, `READ_PICO.touch`):
`swapXY = true`, `rawMinX/MaxX = 0/1215`, `rawMinY/MaxY = 0/683`, `flipX = false`,
`flipY = true`, `irqActiveLow = true`, `synthesizeConfirm = false`.

The contract (`BoardConfig.h:598-604`) is that `swapXY` is applied FIRST — the
digitizer is rotated 90° relative to the panel — and only then are the per-axis flips
applied. So `rawMin/MaxX/Y` describe the **post-swap (panel) axes**, and
`normalizeTouchPoint` divides by those ranges before `GfxRenderer::tapToLogical`
multiplies by `panelWidth`/`panelHeight` and applies the orientation.

The ground truth is the reference firmware's own frame, not an analogy:

- `main/ui/ui_kit.h:26-27` — `UI_x = rawX`, `UI_y = rawY`, in a **portrait 684 x 1216**
  logical frame.
- That frame is reached from the 1216 x 684 panel scan via
  `epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT)` (`main/app/app_loop.c:196-197`).
- `ui_key_hit_test` splits the three key zones by **`rawX`** with `UI_KEY_AREA_TOP`
  1300, centres 80/240/400, pitch 160 — i.e. the strip is beyond `rawY` 1300, at the
  bottom edge, spread left/middle/right by `rawX`.

So the digitizer's X is the user's horizontal (0..683) and its Y the vertical
(0..1215), and the UI consumes exactly that. The reader runs `Orientation::Portrait`,
whose `tapToLogical` is

```
outX = panelHeight-1 - ny*panelHeight      // 683 - ny*684
outY = nx*panelWidth                       // nx*1216
```

(`GfxRenderer.cpp:1995-1999`). Requiring the vendor's `UI_x = rawX`, `UI_y = rawY`
through that transform gives

- `outY = nx*1216 = rawY` → the config's X axis must carry `rawY` → **`swapXY = true`**
- `outX = 683 - ny*684 = rawX` → `ny` must be `1 - rawX/683` → **`flipY = true`**

with `rawMaxX = 1215` (panel X) and `rawMaxY = 683` (panel Y), and `flipX = false`.
Substituting the four measured corners (from the `TOUCH_PROBE_DEBUG` probe):

| user's corner | raw | mapped | vendor UI |
|---|---|---|---|
| top-left | ( 54, 44) | ( 53, 44) | top-left |
| top-right | (629, 64) | (629, 64) | top-right |
| bottom-right | (632, 1205) | (632, 1206) | bottom-right |
| bottom-left | ( 41, 1198) | ( 40, 1199) | bottom-left |

i.e. the mapping returns the raw coordinates — the vendor's 1:1 semantics.

**Why the earlier corner probe misled us.** The probe printed `raw=(x,y)` next to the
output of `mapTouchPoint`, and with `swapXY = false, 683x1215` all four corners did land
where they were tapped *in that frame*. But `mapTouchPoint`'s output is not what the UI
consumes — `tapToLogical` rotates it again for `Orientation::Portrait`. The probe was
validating the wrong frame, so "corners land where tapped" was true and irrelevant at
the same time. The symptom that exposed it was a top-bar tap `raw(339,32) → n=(0.496,
0.026)` opening the reader instead of the top bar.

**Two wrong guesses are recorded so they are not repeated:** `swapXY = true` chosen
alone on M5Paper's precedent (`BoardConfig.h:1277-1288`) transposed the axes; reverting
to `swapXY = false` made `tapToLogical` rotate the frame 90°, which is what put a
top-bar tap onto a middle book row.

**Lesson recorded here because it cost several build cycles:** on this board the axis
question is not answerable by reading another board's fix, and it is not answerable by
probing any single frame in isolation either. It is answered by starting from the
vendor's UI frame and inverting the exact transform the UI applies. Two of the three
corrections in this round came from cross-board reasoning and were wrong.

`TOUCH_PROBE_DEBUG` and its three print sites have since been removed; the flag and the
prints are gone from `platformio.ini` and `InputManager.cpp`.

### 6.8 Still to settle on hardware

- Whether the epdiy backend lands the image in the same scan order as the old path
  (both consume the same 1216x684 1 bpp frame, so no code-level difference is
  visible), and the axis directions / `flipX`/`flipY`.
- Ghosting and drive quality per `RefreshMode` now that the vendor's 2-D tables are
  used instead of the projection (B3).
- Whether the sleep/shutdown page paints under every `sleepScreen` setting.
- Sleep/off current for the new backend.

### 6.8a The PMU power key had no consumer at all

Reported as "短按实体按键关机并不生效" (a short press of the physical key does not power
off). It was not a threshold or a timing problem — **nothing on this board ever reported
`BTN_POWER`:**

1. `READ_PICO.input` is `{PIN_UNASSIGNED x7, false}` (`BoardConfig.h:1876-1877`) — there
   is no host GPIO for the key.
2. `InputManager::getPhysicalState()` therefore never sets `BTN_POWER` on this board;
   `isDigitalPressed(-1)` is false (`InputManager.cpp:356-360`).
3. So the only power action reachable on Read Pico, `main.cpp:987-997`
   (`gpio.isPressed(BTN_POWER) && gpio.getPowerButtonHeldTime() >
   SETTINGS.getPowerButtonDuration()`), could never fire.

That single path covers BOTH gestures: `CrossPointSettings.h:467-468` returns `10` ms
for `SHORT_PWRBTN::SLEEP` and `400` ms otherwise, so "short press to sleep" and "hold to
sleep" are the same comparison against a different threshold. The `SHORT_PWRBTN::SLEEP`
short-press path is not a separate route. (`main.cpp:999-1009`'s click path is
`#if FREEINK_DEVICE_PAPERMONO`, and the `wasReleased(Power)` route at `main.cpp:1005` is
inside it, so neither runs here.)

**Fix.** `keyStripHook()` — already installed as `InputManager::setButtonHook()` and
already OR-ed into the shared state mask — now also publishes the key level:

- The key is on the CW32L010, not a GPIO. `read_pico_pmu_protocol.h:71` defines
  `PMU_STATUS_KEY_PRESSED (1u << 5)`, a **LEVEL** in the STATUS flags word.
- `read_pico_pmu.c` `parse_status` locates that word at `raw[4..7]` (u32 LE); the port's
  `pmuParseStatus()` now decodes it into `g_pmuKeyDown`.
- `keyStripHook()` calls `pmuPoll()` on the vendor's own cadence
  (`read_pico_pmu.h` `KEY_POLL_MS = 50`) and ORs `1 << BTN_POWER` into its mask.
  **50 ms, not every tick:** the CW32 shares SDA39/SCL40 with the CST836U, a 64-byte
  STATUS read costs ~1.5 ms of bus at 400 kHz, and touch reads are latency-sensitive.
  50 ms is also the CW32's own shadow update rate, so polling faster repeats the value.
- A failed poll clears the level rather than holding it. Reporting the key stuck down is
  a shutdown, not a dropped sample.
- Because it is published as a *level* through the existing hook, nothing in the shared
  input layer changes: the debounce, the release edge, `getPowerButtonHeldTime()` and
  both thresholds all work as they do on every other device.

**Deliberately not done:** the hook does not drain the PMU event FIFO
(`PMU_EVENT_FIFO_DEPTH = 8`, `EVENT_PEEK`/`EVENT_ACK`). The level is independent of the
FIFO, `pmuPowerOff()` already drains events at shutdown, and acking events on every key
press in the input hot path would touch PMU state without being asked. If the FIFO is
ever observed full (`PMU_STATUS_EVENT_PENDING`) this is the place to add a bounded
drain. The PMU's own `KEY_FORCE_OFF` remains the hardware backstop for a long hold.

### 6.9 Inputs: middle strip key is Back, and the reader header tap pops

Two input changes land with this round; both are compile-gated per device and neither
touches another board.

**The middle capacitive strip zone is `BTN_BACK`** (`BoardReadPico.cpp` `keyStripHook`,
`kStripKey2Mask = 1U << InputManager::BTN_BACK`). It used to be `BTN_CONFIRM`. The
index is the whole story: the `setButtonHook()` seam can only OR a `1 << BTN_*` mask
into the physical state (`InputManager.cpp:617-619`), after which it rides the ordinary
debounce/edge machinery, so `BTN_BACK` is exactly what `MappedInputManager` consumes,
and the default `SETTINGS.frontButtonBack = FRONT_HW_BACK = BTN_BACK` reaches it with no
remap. Selection moved to the touch panel: the reader menu is a centre-third tap
(`ReaderUtils.h` `isTouchMenuTap`) and list rows are tapped directly. Caveat:
`ButtonRemapActivity` can rebind `frontButtonBack`, after which the middle zone follows
the remap — the same exposure the previous CONFIRM mapping had.

**Reader touch needs NO change.** `wasHeaderTapBack()` was briefly widened to
`FREEINK_DEVICE_READPICO` on the reasoning that it was "the only reader-touch behaviour
eego-a4 has and Read Pico does not" — a source-only comparison, and it was NOT asked
for. It was reverted: making a header tap mean Back steals the top bar's own taps, and
the tester reported exactly that ("屏幕上的 ui 都点不上，像顶部的栏目"). `wasHeaderTapBack`
and its two consumers in `wasPressed`/`wasReleased` are eego-a4-only again.

Everything else is already shared and already active: page-turn taps (outer thirds),
swipe page turn, the centre-tap reader menu, EPUB link taps and the toolbar menu style
(`ReaderUtils.h:78-122`, `ReaderActivity.cpp:148-151`, `main.cpp:648`), gated only by
`SETTINGS.touchReaderControls` (default `TOUCH_READER_ON`) and
`MappedInputManager::hasTouch()` → `InputManager::hasTouch()` → `touchDataEnabled`. No
`#if` was added for those, and none is needed.

Back is reachable two ways on this board: the middle strip zone, and the
device-agnostic left-edge swipe. If a third in-content route is ever wanted (a header
tap, a bottom-edge swipe), it has to be asked for explicitly, because every one of
them takes a tap region away from the UI.

**Touch polling must not be gated on INT#.** `pollCst836u()` used to require INT# to be
low, or a 500 ms idle heartbeat (`CST836U_IDLE_HEARTBEAT_MS`, whose own comment said
"chosen bounds, not measured values"). The reference firmware calls `cst836u_read()`
unconditionally on every loop iteration
(`read_pico_firmware/main/app/app_loop.c:178-184`) and never uses INT# at all. Gating on
it starved the idle period: swipe frames were dropped, a drag came out as a tap, and
the three strip keys needed a lucky sample. Fixed to a fixed 8 ms floor (125 Hz) with
the failure back-off kept. Reported fixed on hardware.

### 6.11 EPUB inline images were written at the wrong place (16-bit framebuffer index)

Reported as "epub 的图片显示错位". Text was fine; only inline images were displaced.

**Cause: a `uint16_t` framebuffer index in the image fast path.**

| Path | Code | Index type |
| --- | --- | --- |
| Text, lines, rects — `GfxRenderer::drawPixel()` (`GfxRenderer.cpp:620`) | `const uint32_t byteIndex = rowY * panelWidthBytes + (phyX / 8);` | `uint32_t` |
| **All** inline images — `DirectPixelWriter::writePixel()` (`DirectPixelWriter.h:160`) | `const uint16_t byteIndex = static_cast<uint16_t>(sy * displayWidthBytes + (phyX >> 3));` | `uint16_t` |

The framebuffer is `displayWidthBytes * panelHeight` bytes. Read Pico's panel is
**1216 / 8 = 152 bytes per row x 684 rows = 103,968 bytes**, so the largest legal index
is 103,967 — far past `uint16_t`'s 65,535. Every index above 65,535 wrapped to an
earlier row, so image pixels landed roughly `65536 / 152 = 431` rows away from where
they belonged and parts of the image reappeared displaced.

**Why no other board ever hit it:** the index only fits in 16 bits while
`stride * rows <= 65535`.

| board | stride (B) | rows | max index | overflow |
| --- | --- | --- | --- | --- |
| X4 | 100 | 480 | 47,999 | no |
| X3 | 99 | 528 | 52,271 | no |
| **Read Pico** | **152** | **684** | **103,967** | **yes** |

Read Pico is the first target whose framebuffer exceeds 64 KB, which is exactly why the
type was silently too narrow. Text was unaffected because `drawPixel()` already used
`uint32_t`, which is also why the symptom looked like an image-only bug.

**Fix.** `uint32_t byteIndex` in `DirectPixelWriter::writePixel()`. This is a pure
widening of a value that **provably** cannot exceed 65,535 on any other target (table
above), so those boards' generated code and behaviour are unchanged — no `#if` needed,
and none was added.

**Everything image-shaped goes through this one function.** `fb[...]` is written only in
`DirectPixelWriter::writePixel()`; `ImageBlock.cpp`, `JpegToFramebufferConverter.cpp` and
`PngToFramebufferConverter.cpp` all call it (the cache path uses the separate
`DirectCacheWriter`, which is `int`/`size_t` based and band-local). A repo-wide check for
the same pattern found no other site, and `HalDisplay::BUFFER_SIZE` was already
`uint32_t`.

**Latent upstream bug.** Any future panel whose framebuffer exceeds 64 KB would hit this,
so the fix is worth keeping even though only this board currently trips it.
