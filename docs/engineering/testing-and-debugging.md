# Testing, Debugging & Verification

> Deep reference for [CLAUDE.md](../../CLAUDE.md). Build/quality commands, the
> crash playbook, the agent vs human verification split, CI awareness, and live
> serial debugging. For the contributor-facing quick version see
> [../contributing/testing-debugging.md](../contributing/testing-debugging.md);
> for webserver issues see [../troubleshooting.md](../troubleshooting.md).

## Build Commands

**Via CLI**:
```bash
# Build firmware (default environment)
pio run

# Build and upload to device
pio run -t upload

# Build specific environment
pio run -e gh_release

# Build and run a native device simulator
pio run -e simulator -t run_simulator
pio run -e simulator_x3 -t run_simulator
pio run -e simulator_eego_a4 -t run_simulator
pio run -e simulator_murphy_m4 -t run_simulator

# Clean build artifacts
pio run -t clean

# Upload filesystem data (if using SPIFFS/LittleFS)
pio run -t uploadfs
```

**Via VS Code**:
* Use PlatformIO toolbar: Build (✓), Upload (→), Clean (🗑️)
* Or Command Palette: `PlatformIO: Build`, `PlatformIO: Upload`, etc.

## Monitoring and Debugging

```bash
# Enhanced monitor with color/logging (recommended)
python3 scripts/debugging_monitor.py

# Standard PlatformIO monitor
pio device monitor

# Combined upload + monitor
pio run -t upload && pio device monitor
```

**Via VS Code**: Click Monitor (🔌) button in PlatformIO toolbar

## Code Quality

```bash
# Complete local CI suite: format, static analysis, firmware builds, host tests
./bin/ci-check

# Check formatting without modifying files
./bin/clang-format-fix --check

# Apply formatting
./bin/clang-format-fix
```

## Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   - Monitor heap usage throughout activity lifecycle
   - Check if large allocations (>10KB) occur before crash
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   - Occurs during deep recursion or large local variables
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   - Reduce the frame or reuse an existing bounded scratch buffer; use a
     checked nothrow heap allocation only when no scoped scratch is available

3. **Use-After-Free**:
   - Activity deleted but task still running
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   - Delete `.crosspoint/` directory on SD card
   - Forces clean re-parse of all EPUBs
   - Check file format versions in [../file-formats.md](../file-formats.md)

5. **Watchdog Timeout**:
   - Loop/task blocked for >5 seconds
   - Add `vTaskDelay(1)` in tight loops
   - Check for blocking I/O operations

### EPUB memory fallback on devices without PSRAM

Styled chapter parsing stops below 48 KiB free heap or 16 KiB largest block.
Allocation failures also stop parsing, including table and trailing-page layout.
The reader discards the failed build and retries once with embedded CSS disabled
for that reading session, retaining the selected font and content offset. XML
and I/O errors do not trigger this retry. A failed basic build shows the existing
index-failed popup. No failed build may become a complete or partial cache.

`EpubReaderActivity::handleBuildFailure()` owns the retry decision, reader
resource cleanup and failure latch for foreground and background builds. Callers
pass `Section::BuildError` explicitly, including `OutOfMemory` when the Section
object itself could not be allocated. The
error popup only draws. `Section::releaseBuildResources()` closes and removes
uncommitted resources without deciding whether an existing cache is valid:
initialization failures preserve it, while `abandonBuild()` invalidates it after
a parse failure. Cleanup leaves `BuildError` available to the reader.

Before starting or continuing a chapter, low-heap no-PSRAM builds reclaim
rebuildable font caches. Optional font prewarm must leave 16 KiB free heap and
8 KiB contiguous headroom; a skipped cache uses on-demand reads. Basic layout
keeps the 320-token soft-flush threshold instead of reverting to 750 tokens.

Build and run the `ChapterHtmlSlimParserTest`, `SdCardFontMemoryTest`, `PageLinkTest`,
`footnote_list_test`, and `CssParserTest` host targets for fault injection,
text/offset preservation, and cache capability checks. These include each mini-cache
replacement allocation failing, initialization failures preserving an existing
complete/partial cache, and a pre-refactor mixed-text cache/LUT digest. The digest
uses the host page-serialization stub; it does not establish pixel equivalence.
`ReaderBuildFailureTest` compiles the actual failure-handling method with
lightweight collaborators for no-PSRAM and PSRAM policies. It covers allocation
failure before a Section exists, one CSS retry, target preservation and repeated
failures; it is not a full Activity or device-lifecycle integration test.
Build firmware with
`pio run -e default -e simulator_x3 -e murphy_m4`.

Hardware acceptance still requires **both X3 and X4**: open the reported EPUB
with a cold section cache using LXGW WenKai size 18, turn across chapters, and
reopen it. Check text against the source for missing or reordered words/lines.
Capture `SCT` cache-reclaim, `SDCF` prewarm-budget, `EHP` failure-stage and
`ERS` fallback/ready/error logs, including
`free/min/maxAlloc`. Under additional pressure, basic styling or an index error
is acceptable; a restart or silently incomplete text is not. Exit and reopen to
confirm the next session attempts the user's embedded-style setting again.
Compilation and simulator checks do not establish hardware acceptance.

### September 6, 2026: reader memory hardening checkpoint

This checkpoint is **not full hardware acceptance**. The preceding X4 candidate
application had SHA256
`7b030e735aa636b96360ce5c7b34d0152526f00edbde601496a9a8800c4b23fa`.
Application readback matched that candidate and the then-current build byte for
byte; the indexing failure was not an older firmware image.

With the reported Jobs biography, LXGW WenKai size 18 and Flash font reads:

| Observation | Result |
| --- | --- |
| Ordinary reopen at spine 41, saved page 2 | CSS retry occurred; basic layout failed after page 6 |
| Failing line-break workspace | 334 tokens, 2,672 bytes requested; maxAlloc 2,036 bytes |
| Free heap after CSS-retry cleanup in failing session | 36,756 bytes |
| Same candidate after a commanded reset | Spine 41 completed all 11 pages; restored offset 390/page 2 and continued into spine 42 |
| Free heap after CSS-retry cleanup following reset | 55,808 bytes |

The 19,052-byte difference identifies a session-dependent memory baseline, not
its owner. Standby clock synchronization/network lifetime is a hypothesis for
follow-up, not a confirmed leak or a fix included in this checkpoint. A reset
that permits reading does not resolve the ordinary-reopen failure.

The phase-closing candidate application has SHA256
`e64b85aa5c8b0f297526a75cdcf66c143211ad7a753ee9ba0bf0b0d9b9cd3da3`
and is 5,905,008 bytes. Its production source is recorded in commit `ef006bf0`;
use the binary hash to identify it, since the build began before that commit.
All 63 targeted host checks, clang-format checks, and the `default`,
`simulator_x3`, and `murphy_m4` builds passed. The hardware builds reported an
existing WebSockets dependency warning about deprecated `NetworkClient::flush()`.
X4 application flashing and hash verification succeeded; a separate capture
started for this candidate. Reading/visual acceptance remains incomplete and
must not inherit the preceding candidate's results.

Keep X3 acceptance, SD-direct font mode, and physical text completeness separate.
For subsequent candidates, record the new application hash and repeat cold-cache
opening, spine 5 and 41, cross-chapter turns, TOC navigation, exit/reopen, and
standby/clock-sync followed by reading. An index error during ordinary use fails
acceptance; safe error handling is acceptable only in additional-pressure tests.
Do not infer hardware acceptance from host serialization stubs or successful
firmware builds. Binary images, original books and device-specific raw logs are
local artifacts and are not committed to the repository.

### C3 Bluetooth page-turner development validation

The shared X3/X4 `default` profile's BLE enablement was reverted after the
candidate failed X4 font-load acceptance on September 6. `c3_ble` remains an
inactive configuration for diagnosis; default/release builds do not inherit it.
The internal-heap gates remain 80/32 KiB (free/largest) for reader startup and
70/24 KiB for settings. Do not lower them merely to make pairing work.

Run the focused checks with:

```bash
cmake -S test -B build/test
cmake --build build/test --target ble_input_internal_tests ble_input_psram_tests \
  ble_input_unavailable_tests ble_overlay_tests ble_ipc_stack_tests ble_key_mapping_tests
ctest --test-dir build/test --output-on-failure -R 'Ble|Nimble|internal\.|psram\.|unavailable\.'
pio run -e default
pio run -e gh_release
pio run -e murphy_m4
```

To reproduce the BLE candidate, add a local-only profile in
`platformio.local.ini` (the same binary detects X3 and X4 at runtime):

```ini
[env:c3_ble_probe]
extends = env:default
lib_deps = ${c3_ble.lib_deps}
extra_scripts = ${c3_ble.extra_scripts}
custom_nimble_config = ${c3_ble.custom_nimble_config}
custom_sdkconfig =
  ${firmware_tuned.custom_sdkconfig}
  ${c3_ble.custom_sdkconfig}
build_flags =
  ${env:default.build_flags}
  ${c3_ble.build_flags}
  -DCROSSPOINT_VERSION=\"${crosspoint.version}-dev-c3ble\"
```

Build it with `pio run -e c3_ble_probe`. This diagnostic profile is not a
supported default and does not establish reading stability.

`BleC3Config` checks profile isolation and host overrides against conflicting
core defaults; `BleHostStartup` compiles the SDK's actual task-creation tail
with a failing FreeRTOS allocator, checking cleanup and a successful retry.
The host suites use doubles and do not measure the real Bluetooth stack.

For each candidate, archive the BLE-off baseline and BLE-on application's
SHA256, ELF RAM/flash sizes, core `sdkconfig.h`, and effective NimBLE macros.
Inspect both the NimBLE and SDK host compiler commands: C3 must include
`NimbleC3Config.h`, use internal memory, Central/Observer only, one connection
and MTU 23, and omit the S3 IPC wrapper. Core and host configuration are
separate: the core must be controller-only. Compile-time sizes do not include
all runtime controller, host, connection-task and GATT allocations.

September 6, 2026 build comparison (application source `cea786ea` plus the C3
BLE changes; the SDK task-creation fix is inactive in the BLE-off build):

| Measurement | BLE off | BLE on |
| --- | ---: | ---: |
| PlatformIO static RAM | 57,420 B | 65,284 B |
| IRAM reservation (`.dram0.dummy`, including alignment) | 67,072 B | 88,064 B |
| Program size / 6,553,600 B application partition | 5,897,381 B | 6,127,729 B |
| Application image file | 5,911,072 B | 6,141,728 B |

The static DRAM increase is 7,864 B; the IRAM reservation increases another
20,992 B. These share C3 SRAM, so the combined increase is about 28.2 KiB before
runtime Bluetooth allocations, even with the runtime setting off. This is not
a measured free-heap delta. The image was identified as an ESP32-C3 application
with valid checksum/hash by esptool; subsequent X4 results are recorded below.

Application SHA256 (whole `.bin`, not esptool's embedded ELF/validation hash):

- BLE off: `608562a77b6493f37835ce2cbb15df13f91ea5eaf3fae105619738ccf845ca5d`.
- BLE on: `695fec10aa4e0ab815adf28b8bfc880b36b8f15f1fcaaf9098d1d7f595613e9d`.

All 34 focused host checks passed. The actual C3 NimBLE/SDK compiler commands
include the C3 configuration header and omit the IPC wrapper; preprocessing
against the generated controller-only core and pinned NimBLE headers confirmed
the effective host settings. The enabled candidate (then built as `default`),
`gh_release` and `murphy_m4` firmware builds passed; `gh_release` contains no NimBLE host, and the S3 build retains
its existing PSRAM host/IPC configuration. The builds still report upstream
dependency warnings (including WebSockets' deprecated `NetworkClient::flush`).
X4 application flashing and an independent `verify-flash` digest check passed
at `app0` (`0x10000`). Runtime logs identified `xteink_x4` and application
`1.5.8-dev-main-cea786ea`, and the home page started. Before any BLE startup,
the candidate failed to allocate 4,000 full Unicode intervals for
`LXGWWenKai_18.cpfont` (48,000 contiguous bytes), both from the Flash cache and
on its SD retry. The existing font-error path then cleared and saved the user's
SD font selection. This candidate has not passed normal-reading acceptance.

The BLE-off baseline was restored to the device, with write-hash verification.
It loaded the fallback `NotoSansSC_12.cpfont` successfully. The resulting states
are **not a same-font A/B comparison**, because the candidate's error handler
changed the saved selection:

| Observed home-page state | Free heap | Total heap | Minimum free | Largest block |
| --- | ---: | ---: | ---: | ---: |
| BLE candidate, WenKai failed and was unloaded | 83,780 B | 238,428 B | 50,112 B | 38,900 B |
| BLE-off baseline, fallback NotoSansSC loaded | 62,228 B | 268,928 B | 60,620 B | 51,188 B |

Subsequent baseline interaction loaded WenKai size 16 from both SD and Flash
successfully. Size 18 with Flash font caching still needs to be restored before
claiming a controlled same-font comparison or attributing the precise failure
to BLE alone. After reverting default enablement, the `default` firmware rebuilt
successfully with no NimBLE host in its ELF; all 34 focused checks passed again.
Pairing, physical page turns, reconnect cycles, 30-minute reading and X3 hardware
acceptance remain unverified. Do not lower the BLE gates or change reading-font
behavior just to make this candidate pass.

Later on September 6, the same BLE candidate was reflashed to X4 `app0` at
the user's request for pairing diagnosis. Write-hash verification and an
independent `verify-flash` check passed, and the device rebooted to the home
page. Only the application partition was written. WenKai size 16 failed the
same interval allocation in both Flash and SD paths; the existing error handler
cleared the saved font selection, and fallback NotoSansSC size 12 also failed
on the next boot. The device now retains the BLE candidate without automatic
rollback; the source `default` profile remains BLE-off. Device interaction
entered `BluetoothSettings`, passed the unchanged settings gate with
78,876/38,900 B free/largest, and logged `BLE HID host started`. Immediately
after startup, free/largest were 29,252/24,564 B. The last captured running
sample was 22,352/19,444 B; NimBLE and connection-task stack headroom were
2,032 and 2,200 B. This confirms menu access and host startup; pairing and
physical page turns remain unverified.

#### C3 coexistence optimization candidate

The follow-up candidate keeps source `default` BLE-off. It combines the C3
Flash controller with callback-only scan results, 12 host HCI events, and paged
coverage indexes for C3 BLE fonts whose resident table would exceed 4 KiB.
The installed NimBLE 2.3.8 `nimconfig.h` defines the event count unconditionally,
so the C3 override loads that header first. Verify the final preprocessed
NimBLE and SDK host units, not just `sdkconfig.h`. Pioarduino also leaves the
stock `-lbtdm_app` after its core rebuild; the C3 post script replaces that
link input with the matching ESP-IDF `libbtdm_app_flash.a` without editing the
shared package. The final link map must also confirm that `r_ke_init` resolves
to Flash code, not an absolute ROM address: selecting the archive alone is
insufficient (see the startup crash below).

The font browser now requests redraw after manifest success and failure.
Previously either transition could leave the loading screen visible until
another event. Network diagnostics use existing BLE heap logging at Wi-Fi
mode changes, connection, HTTP completion/failure, and font manifest parsing.
Underlying SecureClient logs distinguish TCP, TLS allocation and handshake
failures. Neither network symptom has yet been reproduced in a captured trace;
memory pressure is supported by the measurements but is not a verified sole
cause of the reported OTA failure.

Initial optimization build and X4 boot results (September 6, 2026):

| Measurement | Original BLE candidate | Optimized candidate |
| --- | ---: | ---: |
| Static DRAM | 65,284 B | 64,796 B |
| IRAM reservation | 88,064 B | 68,096 B |
| Program size | 6,127,729 B | 6,087,413 B |
| Application image | 6,141,728 B | 6,101,408 B |
| 4,000-entry full coverage index buffers | 48,000 B | 884 B |

The combined static SRAM reservation decreases by 20,456 B. The index-buffer
comparison excludes small state and allocator overhead. Boot now loads
`NotoSansSC_12.cpfont` from SD successfully (177 ms observed initial load), with
101,192 B free heap, 51,188 B largest block and 63,280 B minimum free at home.
The original candidate had failed to load that font. These are different
loaded-font states, not an equivalent free-heap benchmark.

Application SHA256:
`6cf0d6342dd1f2397f4da0d47fe9fecfd53e4cf70cc57e0ce7587e5e00ef0700`.
X4 application write-hash and independent `verify-flash` digest checks passed.
The device reports version `1.5.8-dev-c3ble-cea786ea`. Flash identification is
manufacturer `85`, device `2018`; the current SDK generic driver does not
advertise suspend support for this manufacturer, so automatic suspend remains
off. Bond writes/deletions and Flash font-cache writes still require physical
acceptance with the Flash controller. Do not infer this from a successful boot.

The 68 focused BLE/font CTest entries passed, including paged/full coverage
agreement, non-BMP and partial-page boundaries, allocation failure, corrupt
intervals, retry after short reads, Flash-to-SD fallback, and SDK scan-result
ownership after temporary advertisement buffers are released.
All four firmware builds passed: `default`, `c3_ble_probe`, `gh_release`, and
`murphy_m4`. The same-source BLE-off baseline uses 57,412 B static DRAM and
67,072 B IRAM reservation, so the optimized BLE profile adds 8,408 B of static
SRAM. Default/release ELFs contain neither NimBLE nor paged font lookup; the
S3 ELF retains its BLE host and IPC wrapper and excludes the C3 Flash controller
and paged lookup. Candidate/SDK effective macros confirm 12 HCI events.
OTA detection loops, font-list loops, connected page turns, WenKai 16/18,
Flash-cached fonts, reconnect cycles and 30-minute reading remain pending.
Artifacts and serial logs for this local run are in
`/private/tmp/crossmux-c3-coexist/` (temporary, not committed).

#### C3 Flash-controller startup crash (September 6, 2026)

The user subsequently confirmed OTA detection and font downloads working, but
enabling BLE crashed. The saved X4 core dump matches the candidate ELF SHA256
`91721b3b31f96fd306b4c1e3a55b0499d9518b9e3bc9007d3cdff297a27f0c2d`.
It identifies `btController`, PC `0x4000a480` (ROM `r_ke_init`), and return address
`0x4223d71a` (Flash `r_rwip_init+66`). Controller stack headroom was 3,680 B;
this trace does not indicate stack exhaustion.

Arduino's fixed link flags still included `esp32c3.rom.bt_funcs.ld` and
`esp32c3.rom.eco3_bt_funcs.ld`, overriding symbols from `libbtdm_app_flash.a`.
ESP-IDF's `components/esp_rom/CMakeLists.txt` explicitly excludes these mappings
(and `eco7_bt_funcs`) for `BT_CTRL_RUN_IN_FLASH_ONLY`. The C3 post script now
does the same while preserving non-BT ROM/errata mappings. The configuration
regression check covers these exclusions; all 68 focused BLE/font checks pass.
The initial candidate's static/Flash measurements above describe the broken
mixed link and must not be treated as the corrected controller's memory cost.
The corrected C3 build passes: `r_ke_init=0x42263f2c` and
`r_ke_event_init=0x42236faa` are text symbols in Flash. Static DRAM is 65,108 B,
IRAM reservation 68,608 B, program size 6,269,927 B, and app image 6,283,920 B.
Compared with the original BLE candidate, combined static SRAM decreases by
19,632 B; compared with the same-source BLE-off baseline it increases by 9,232 B.
The corrected app SHA256 is
`cc2a2d76ca849c676ae61e9ae442ca79011f3f189327ff1f308225e7baf611e5`.
X4 application write-hash and independent Flash digest checks pass. The device
boots with WenKai 16 loaded from Flash (91 ms, 884-byte index), with 100,360 B
free/51,188 B largest block at home. X4 BLE acceptance remains required;
successful linking and boot alone do not close it. Corrected artifacts and
serial capture are under `crash-fixed/` in the same temporary artifact directory.

The pre-fix serial capture also confirms font-manifest download (2,157 bytes),
successful TLS 1.2 fallback, parsing of four families and a display refresh.
Wi-Fi had 32,488 B free/27,636 B largest block after connection; the request
reached 8,600 B minimum free. This is one observed font-list load, not the full
network loop acceptance. WenKai 16 also loaded its paged index from Flash at
boot (92 ms), preserving the selected font.

#### C3 reader freeze: CPU-frequency control candidate

The corrected Flash-controller build connected successfully in Bluetooth settings.
The user then confirmed a separate whole-device freeze on opening an EPUB:
physical page/menu buttons also stopped responding. The trace shows reader BLE
startup succeeding at 88,440 B free/51,188 B largest block, followed by
`Going to low-power mode` and completion of idle prewarm; periodic logs then stop.
This is not a reader startup-gate rejection. C3's manual idle clock is 10 MHz;
`HalPowerManager::setPowerSaving()` previously guarded Wi-Fi but not BLE, and
the candidate has `CONFIG_PM_ENABLE` disabled.

The first isolation candidate changes only HAL frequency protection and logs;
font, grayscale and cache behavior stay identical. C3 BLE host lifetime now
blocks idle downclocking, including scan/reconnect states; stopping the host
restores normal idle policy. The host regression executes the actual HAL method
with BLE-on/off and non-C3 configurations, plus Wi-Fi and scoped-lock checks.
All 68 focused BLE/font CTest entries pass.

Application SHA256:
`c938920873d538f73b85e49ef61917e4ebb2942ddc666c2f4dad95909cc1c93f`.
Static DRAM is 65,116 B, IRAM reservation 68,608 B and app image 6,284,112 B.
X4 write-hash and independent Flash digest verification pass; boot and non-BLE
idle at 10 MHz are observed. Artifacts/logs are in
`/private/tmp/crossmux-c3-coexist/power-fixed/`.
The user confirmed that physical controls and BLE page turning work in the book
with this frequency-protection candidate. A later chapter-selection test exposed
a separate recovery failure; this does not close the endurance matrix below.
The reader BLE menu and resource-policy changes follow recovery validation.

The subsequent chapter-recovery trace showed 93,904 B internal free heap but
only a 29,684 B largest block, below the unchanged 32 KiB reader gate. It also
showed a host start immediately followed by a chapter-build stop. A missing
section now defers **new starts**, while a reader menu retains an existing host.
All four EPUB build entrypoints stop BLE before build allocation. C3 BLE builds
release only rebuildable SD font arenas at that boundary, retaining the font and
coverage index. No framebuffer reallocation or rendering-option change is made.

For a C3 reader with Bluetooth enabled, an incremental build prepares a window
20 pages ahead (the existing 15-page restart margin plus the 5-page window), then
uses `Section::suspendBuild()` to persist a partial cache and release the parser.
Approaching its watermark resumes the existing build path. Explicit full-index
operations and unresolved saved-position remapping retain their completion
requirements. Partial commit failure releases resources and invalidates the
in-memory page count rather than exposing a possibly removed old cache. Other
platforms and Bluetooth-off reading retain their original build window.

The diagnostic candidate logs chapter preparation, font arena addresses/sizes,
parser/context/LUT addresses/sizes, post-release heap, and BLE task handles and
stack margins. Repeated identical startup gate/host failures still retry every
two seconds but do not repeat the diagnostic group. The first insufficient
contiguous block dumps the internal heap map once per boot. These diagnostics
must establish the remaining allocation owners before calling fragmentation
resolved; moving cache reclamation earlier alone is not evidence of a fix.

Chapter-recovery application SHA256:
`ff99d3bc8cc0ac4e84a3180c5917684e4c85f45fe3e9a1cd79c781ad927588fd`.
Static DRAM is 65,124 B, IRAM reservation 68,608 B and image 6,285,776 B.
All 71 focused BLE/font CTest entries pass, including execution of the actual
lifecycle, background-build, suspend-failure and gate-log paths. Build artifacts
and the hardware trace are in `/private/tmp/crossmux-c3-coexist/chapter-recovery/`.
The Flash controller archive and C3 ROM-link isolation remain verified. Source
default/release BLE stays disabled; this is an opt-in diagnostic candidate.

Before broader acceptance, use the same book for at least 20 chapter transitions:
open/cancel the chapter menu, select a cached chapter, select an uncached chapter,
turn across chapters in both directions, and exit/reopen. With the remote awake,
record time from `Build resources released` to host startup/HID connection.
Check physical page/menu keys in each case, no needless prepare/start/stop cycle,
no persistent 32 KiB gate rejection, and stable recovered free/largest heap.
C3 BLE, default, gh_release and S3 murphy_m4 builds all pass. X4 application
write-hash and independent digest verification pass, and the new boot retains
WenKai 16 from Flash font cache. Initial shelf free/largest heap is stable at
100,424/51,188 B. The user subsequently reported that the three requested
same-book scenarios worked, but opening another book did not reconnect BLE.
The capture ended at 00:32 before those operations. A read-only serial attachment
at 00:53 preserved the failed device state (no DTR/RTS changes or reset) and
observed repeated free/largest readings of 93,612/30,708 B, with historical
minimum 5,016 B. The main loop is alive and the largest block is below the reader
32 KiB gate; the responsible allocations and the failing book format are not
yet established. Same-book user feedback does not close cross-book recovery,
fragmentation or the quantitative endurance matrix. Current evidence is in
`/private/tmp/crossmux-c3-coexist/book-recovery/x4-serial.log`.
A restart comparison then produced four successful connections, including a
chapter-build recovery. Startup largest blocks were 51,188, 42,996, 42,996 and
40,948 B; these are different book/chapter states, not a controlled leak trend.
The user confirmed both books reconnect and turn pages. Fixed-page alternating
book cycles are requested to distinguish retained allocation layout from a
repeatable per-cycle decline; the earlier persistent gate failure remains open.
The user reported five alternating-book rounds passed after that restart.
Logs also contain page turns, partial-cache extension to 33 pages, and first-time
JPEG cache decoding, so the run is not a fixed-page allocation comparison.
Observed reconnects succeeded at largest blocks of 42,996 and later 36,852 B;
there is no new startup gate rejection in this boot. Connected idle free/largest
then stabilized at 43,496/32,756 B (the startup gate is not applied to an already
running host). No panic/reset banner appeared in the observed exercise. The
original persistent 30,708 B rejection remains unexplained; next exercise is
continued reading through another partial-cache extension without rebooting.

That continuation exposed another rejection: at 01:20:19, spine 11 had completed
rendering but free/largest was 92,292/32,756 B after cache reclamation. It stayed
below the 32,768 B gate for at least 57 seconds. Entering spine 12 at 01:21:24
restored a 42,996 B largest block; after partial-cache construction, BLE connected
at 01:21:44. The user's eventual-recovery report is valid, but the persistent
gate rejection remains open. Two PNG decodes in spine 11 were also rejected by
the existing 59,456 B contiguous-working-set requirement.

The promised first-failure heap map was missing: IDF's `heap_caps_dump()` uses
`esp_rom_printf`, whereas this X4 application's log transport is USB. The next
diagnostic copies eight walker entries at a time and logs through `LOG_INF`
only after the heap lock is released. It uses 112 B of local C3 stack, no new
heap allocation, and runs only on the first block rejection per boot. The
batched map is not atomic across other tasks. Section path address/capacity is
also logged to help identify retained allocations. The host check executes the
production dumper with unordered heaps, multiple batches and a lock assertion.

Diagnostic image SHA256:
`c8c3488667be214a98ceb0477c64d9799885f6fa5a418231e4d48d7da2579e59`.
C3 build and all 71 focused CTest entries pass. Static DRAM remains 65,124 B,
IRAM reservation 68,608 B; image size is 6,286,208 B. X4 application write-hash
and independent digest verification pass. Artifacts and the next serial trace
are in `/private/tmp/crossmux-c3-coexist/usb-heap-diagnostic/`. This image repairs
diagnostic visibility only; it does not establish a fragmentation fix or change
BLE thresholds, font selection, antialiasing or cache policy.

At 01:41 on 2026-09-07, the user reported normal operation on this diagnostic.
The roughly six-minute trace contains a successful connection and no new BLE
gate rejection, start failure or panic. Connected internal free/largest heap
was 46,076/42,996 B; observed current/NimBLE/connection/render stack remaining
was 2,704/2,900/2,240/4,984 B. Two PNG decodes were still rejected at 01:37
(largest 40,948 B versus 59,456 B required). Record this as a successful BLE
observation, not closure of the earlier intermittent fragmentation or PNG
memory issue; the 30-minute endurance criterion is not established by this trace.

X4 acceptance requires the following, with the actual remote model recorded:

- Scan, bond, map forward/back, restart and reconnect. Perform at least 100
  separated short presses, checking missed/duplicate turns and popup transitions.
- Read for at least 30 minutes, using cold and complete EPUB caches, cross-chapter
  turns, Chinese SD fonts, and exit/reopen. Record Flash-cached and SD-direct font
  modes separately. Indexing temporarily stops BLE; verify reconnection afterwards.
- Repeat Bluetooth on/off 20 times, then Wi-Fi and sleep/wake transitions.
  Compare warmed-up, equivalent idle states after teardown; historical minimum
  heap alone cannot establish a leak. Record internal free/min/largest and all
  available BLE/render task stack high-water marks (at least 512 bytes remaining).
- Normal reading must not gain indexing errors, resets, persistent disconnects
  or declining recovered heap. Under deliberate memory pressure, refusing to
  start BLE is acceptable. Failure during ordinary reading blocks default-profile
  enablement; retain the diagnostics and revert the enablement configuration.

Report flashing/hash verification, physical key delivery, reading stability and
X3 acceptance separately. Without a connected X4 and remote, hardware checks
remain pending; a simulator or successful firmware link cannot replace them.

### Distinguishing TCP Stalls, Watchdogs, and Restarts

- A task-watchdog failure prints the task watchdog banner and subscribed task
  names before the register dump. A later `RTC_SW_CPU_RST` is the panic restart
  path; it does not make the original failure an intentional software restart.
- A normal software restart has no preceding watchdog or panic report. Check
  the earlier application log for an expected transition such as leaving
  network mode before treating the reset reason itself as a crash.
- A slow HTTP reader can fill the TCP send window and keep a synchronous SDK
  write waiting for several seconds. Correlate the request URI and elapsed
  time with the serial log. Do not classify this as OOM solely from the minimum
  historical heap value; also inspect current free heap and the largest
  allocatable block.
- Web request handlers execute on the task that calls `handleClient()`. Do not
  subscribe that task to the task watchdog merely so handlers can feed it: a
  legitimate SDK network wait can then be misclassified as a CPU lockup. Keep
  explicit watchdog resets conditional so other platform configurations remain
  compatible.

**Verification Steps**:
1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):
1. ✅ **Build**: `pio run -t clean && pio run` (0 errors/warnings)
2. ✅ **Quality**: `./bin/ci-check` and `./bin/clang-format-fix --check`
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Memory**: record `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`,
   `ESP.getMaxAllocHeap()`, and task stack high-water marks. Memory-sensitive
   EPUB paths must still work when the largest contiguous block is about 16–19KB;
   no task may fall below 512 bytes of remaining stack.
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow | File | Purpose |
|----------|------|---------|
| Core Build Check | `.github/workflows/ci.yml` | Builds `default` (shared X3/X4) and `x4pro` |
| Hardware CI | `.github/workflows/hardware-ci.yml` | Builds all simulators and global S3 targets for hardware-sensitive changes or manual runs |
| Format Check | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Firmware Release | `.github/workflows/nightly.yml` | Stable releases from SemVer tags and scheduled/manual Nightly releases |

**Rules**:
- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Hardware CI runs only for its configured paths, or from **Run workflow**
- Format check fails → Run clang-format locally
- Build check fails → Fix compile errors

Firmware build jobs call `select-build-runner.yml` before they start. Trusted
same-repository PRs, pushes, tags, schedules, and manual runs use the H2O
self-hosted runner only when it is online and idle; fork PRs, Dependabot, a
missing `H2O_RUNNER_TOKEN`, API errors, and an unavailable H2O fall back to
`ubuntu-latest`. The token must be repository-scoped with read-only
Administration permission. Selection is best effort: a runner that disconnects
after selection can still leave the selected job queued. Formatting, static
analysis, unit tests, and GitHub publishing remain GitHub-hosted. Nightly COS
publishing is the exception: it requires the H2O labels with no fallback, has
read-only repository contents, and receives COS credentials but no GitHub write
credential. If H2O is unavailable, that regional job waits while the independent
GitHub publish job can continue.

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Standard**: `pio device monitor` (basic, no colors)
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`
