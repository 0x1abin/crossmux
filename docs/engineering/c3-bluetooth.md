# ESP32-C3 BLE page-turner support

All hardware firmware builds include BLE support, including the shared X3/X4
`default`, release, RC and slim profiles. Compilation makes Bluetooth available
in settings; the saved Bluetooth switch still defaults to **off**. Native
simulators retain the SDK stub because they have no BLE radio backend.
X4 users have confirmed pairing, physical page/menu keys, chapter changes,
alternating EPUBs and subsequent reconnection. This is not X3 hardware acceptance
or completion of the quantitative endurance matrix below.

## Ownership and resource policy

| Owner | Responsibility |
| --- | --- |
| `HalPowerManager` | Keep normal CPU frequency throughout the C3 host lifetime, including scan/reconnect. Restore ordinary idle policy after stop. The manual 10 MHz idle clock bypasses IDF power locks. |
| `BleInput` | Apply reader **80/32 KiB** and settings **70/24 KiB** internal free/largest gates. Reader recovery releases rebuildable font caches, retaining the selected font. Identical failure/context logs are suppressed; attempts still run. |
| `main.cpp` | Maintain an existing reader/menu connection, and automatically start only when reading is ready. Wi-Fi, exclusive storage and leaving the reader stop the host. Keep the existing two-second failed-start retry. |
| `EpubReaderActivity` | Share the partial-cache restart predicate between readiness and background construction. Evaluate restart/build/suspend under one render lock. Stop BLE **before** all four construction entrypoints allocate. |
| `Section` | Own and release parser/build resources. Persist a partial cache on suspension; invalidate page counts and report I/O error if the commit fails. |
| SDK `BleKeyboardHost` | Own the fixed device list, bonding, eight-second connection timeout and four-second reconnect cadence. C3 callbacks copy discoveries without retaining NimBLE's duplicate result list. Worker allocation failure cleans up and returns false. |

With C3 Bluetooth enabled, background construction prepares 20 pages ahead
(the existing 15-page restart margin plus five pages), then persists and releases
the parser. Merely pausing parsing would retain its heap. Approaching the partial
watermark resumes the same construction path. Full-index requests and unresolved
saved-position remapping retain their existing completion requirements. Other
platforms and Bluetooth-off reading keep their original build window.

Large C3 BLE font coverage tables use 32-entry pages plus sparse first-codepoint
keys when the resident representation exceeds 4 KiB. A 4,000-interval full table
uses **884 B** of index buffers instead of **48,000 B**, excluding per-style state
and allocator overhead. Allocation is once per loaded style, at most 896 B;
the font owns the buffers until unload. Small tables and other configurations
retain their existing resident indexes. Full validation precedes indexing;
coverage, prewarm and on-demand glyph lookup share one lookup path. Prewarm
batches reuse a lazy file cursor, including Flash-to-SD fallback. I/O failure is
distinct from missing coverage and must not become a cached missing glyph.

Font formats, rendering preferences and antialiasing are unchanged. No extra
reconnect loop, SDK public API, persistent setting or reader menu is introduced.

## Reproducible build

No local override is needed. Build X3/X4 with `pio run -e default`,
`-e gh_release`, `-e gh_release_rc`, or `-e slim`. Each inherits `c3_hardware`,
which supplies the C3 host configuration, tuned controller-only core and Flash
controller link script. The existing S3 hardware profiles supply their PSRAM
host configuration to every firmware flavor, including release and RC.

The common `ble_host` profile pins NimBLE-Arduino 2.3.8 and SDK compatibility
middleware. C3 injects `NimbleC3Config.h` into both NimBLE and the SDK host:
internal RAM, Central/Observer, one connection, four bonds, MTU 23, six blocks
per mbuf pool (256/320 B), and 12 high-priority events. Pool sizes do not describe
total runtime heap. S3 retains its PSRAM allocator and IPC wrapper; C3 uses neither.

The controller-only custom core enables `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY`.
Pioarduino's final Arduino link still needs the matching ESP-IDF
`libbtdm_app_flash.a` and exclusion of `esp32c3.rom.{bt_funcs,eco3_bt_funcs,eco7_bt_funcs}.ld`.
The C3 post script selects these without modifying installed packages. Mixing
the Flash archive with the ROM function tables previously crashed `r_ke_init`.
Check the final map: controller initialization must resolve to Flash text, not
an absolute ROM address. Check effective compiler macros in both host libraries,
not only the generated core configuration.

## Validation and known limits

```bash
cmake -S test -B build/test
cmake --build build/test --target ble_input_internal_tests ble_input_psram_tests \
  ble_input_unavailable_tests ble_overlay_tests ble_ipc_stack_tests ble_key_mapping_tests \
  SdCardFontMemoryTest SdCardFontPagedTest sd_card_font_cache_format_test FontCacheManagerTest \
  ChapterHtmlSlimParserTest
ctest --test-dir build/test --output-on-failure \
  -R 'Ble|Nimble|internal\.|psram\.|unavailable\.|SdCardFont|FontCacheManager|ChapterHtmlSlimParser|Section'
pio run -e default -e gh_release -e gh_release_rc -e slim \
  -e sticky-gh_release -e x4pro-gh_release -e x4c-gh_release -e papermono-gh_release
```

Host checks exercise production lifecycle methods, startup failure/retry, input
isolation, CPU protection, profile/link isolation, paged/resident lookup agreement,
non-BMP and page boundaries, corrupt files, allocation failures, short-read retry
and Flash fallback. They do not model the actual controller or prove radio timing.

### All-hardware enablement (September 7)

All 26 committed hardware environments resolve to BLE-enabled PlatformIO
configurations. The 14 existing S3 development/Nightly environments retain
equivalent flags, dependencies, scripts and core options. The 94 related host
checks and all 43 build/packaging script tests pass; these suites overlap.

Eight firmware builds pass. Every ELF contains the NimBLE host. C3 controller
initialization resolves to Flash and the actual exported core headers enable
Flash/controller-only operation, without S3 IPC symbols. S3 retains PSRAM and
IPC symbols without the C3 paged index or Flash controller; the three USB release
ELFs also retain TinyUSB/MSC. Every application image passes checksum/hash
validation. Download interruptions were resolved without source changes.

| Build | Static DRAM | IRAM reservation | Program | Image | SHA256 prefix |
| --- | ---: | ---: | ---: | ---: | --- |
| `default` | 65,116 B | 68,608 B | 6,269,913 B | 6,283,904 B | `951416473213` |
| `gh_release` | 65,092 B | 68,608 B | 6,218,739 B | 6,232,736 B | `2606e030bf82` |
| `gh_release_rc` | 65,092 B | 68,608 B | 6,218,779 B | 6,232,768 B | `b0f30f369b92` |
| `slim` | 65,092 B | 68,608 B | 6,137,183 B | 6,151,168 B | `da8bd9c9294b` |
| `sticky-gh_release` | 75,724 B | 85,248 B | 5,797,587 B | 5,798,096 B | `8b14e6db1cfa` |
| `x4pro-gh_release` | 99,532 B | 85,760 B | 5,906,130 B | 5,906,640 B | `a9c86613588d` |
| `x4c-gh_release` | 99,332 B | 85,248 B | 5,882,547 B | 5,883,056 B | `9adddf50f428` |
| `papermono-gh_release` | 115,924 B | 85,760 B | 5,913,842 B | 5,914,352 B | `0ecddf432350` |

Full hashes, ELF/map files, actual configuration headers and logs are local under
`/private/tmp/crossmux-ble-all-builds/`. These images have not been flashed; the
hardware evidence and remaining acceptance matrix below remain separate.

### Earlier review and device evidence

Before all-build enablement, the September 7 review passed 71 BLE/font checks,
23 chapter/parser checks, changed-file formatting and the four historical
builds below. At that point, default/release ELFs excluded NimBLE and paged
lookup; the local C3 candidate included them and resolved controller startup
to Flash. S3 retained its host/IPC wrapper without C3 code. The exported compiler
commands and preprocessing of actual NimBLE `ble_hs.c` and SDK host units
confirmed the C3 internal allocator, one connection, MTU 23, Central/Observer
roles and 12 events. Upstream dependency warnings remained (WebSockets
deprecation and Arduino/ESP-IDF dependencies).

| Historical review build | Static DRAM | IRAM reservation | Program | Application image |
| --- | ---: | ---: | ---: | ---: |
| `default` | 57,412 B | 67,072 B | 5,896,729 B | 5,910,416 B |
| `gh_release` | 57,388 B | 67,072 B | 5,845,085 B | 5,858,768 B |
| `murphy_m4` | 102,700 B | 83,968 B | 5,942,034 B | 5,942,544 B |
| `c3_ble_probe` | 65,116 B | 68,608 B | 6,269,957 B | 6,283,952 B |

The review's C3 opt-in profile adds 9,240 B combined static DRAM/IRAM over the
same-source default build. Removing temporary diagnostics reduces the application
image by 2,256 B and static DRAM by 8 B relative to the last flashed diagnostic;
this is not a runtime fragmentation improvement. Artifact SHA256 values:

- `default`: `27912737995efd6fee9daa194eff493dc3ef4aa2aa3b89dc601a18fc206798a8`
- `gh_release`: `c3184e01c91c18769eaa4f658d43809a2fdbbe23ffb5b0c5526ff94f229b5498`
- `murphy_m4`: `2cd5ddcd2904a3e6ae2c1f8967e71cd4306a2dbede8b09ca0384ebd854f02301`
- `c3_ble_probe`: `7af885a132edbe3cb96f6e57bbfe00196789e934d599faa3a4436c1e27bd7116`

Review artifacts are local under `/private/tmp/crossmux-c3-review/`. The C3
application checksum/hash passes `esptool image-info`; it has **not been flashed**.
SDK startup/scan ownership changes were merged in
[FreeInk SDK PR #23](https://github.com/0x1abin/freeink-sdk/pull/23). The gitlink
pins merge commit `5faf69e8fdd4f3f959faa99c8814a38c117d7678`, whose source tree
is identical to the SDK revision used for the review checks above.

September 6–7 X4 evidence before the review refactor:

- Original BLE candidate added about 28.2 KiB combined static DRAM/IRAM versus
  BLE-off and could not allocate a 48,000 B font index. The corrected Flash
  controller candidate used 65,108 B DRAM + 68,608 B IRAM reservation, 19,632 B
  less combined static SRAM than the original BLE candidate. These are ELF
  reservation comparisons, not equivalent free-heap measurements.
- The CPU-frequency fix restored physical controls and BLE page turning. The
  user subsequently confirmed OTA detection and font downloads work. Manifest
  completion now requests redraw on both success and failure. The original OTA
  failure was not captured with a conclusive error code; memory was a supported
  suspect, not a proven sole cause.
- The latest flashed diagnostic SHA256 was
  `c8c3488667be214a98ceb0477c64d9799885f6fa5a418231e4d48d7da2579e59`.
  Application write hash and independent Flash digest verification passed at
  `0x10000`, preserving settings/files. The user reported normal operation.
  At 01:41, connected free/largest was 46,076/42,996 B; observed current/NimBLE/
  connection/render stack headroom was 2,704/2,900/2,240/4,984 B.
- Earlier traces contained persistent largest-block gate rejection at
  30,708 B and 32,756 B despite over 90 KiB free. Restarting or moving to another
  chapter restored connections. Allocation ownership remains unproven. Two PNG
  decodes still failed at 01:37 with 40,948 B largest versus 59,456 B required.
  Normal user feedback does not establish that these memory limits are fixed.

The review removes temporary heap walking and allocation-address tracing from
the application and the SDK, including BLE-specific dependencies in HTTP/font
network paths. Keep ordinary error codes and lifecycle free/min/largest/stack
logs. Historical raw logs/images remain local under
`/private/tmp/crossmux-c3-coexist/`; they are not repository assets. A refactored
image requires its own hardware check and must not inherit an earlier hash's
flash verification.

For X4 acceptance, record the remote model and separately complete: 20 chapter
transitions (cancel/cached/uncached/both directions/reopen), 100 separated short
presses, 20 BLE on/off cycles, at least 30 minutes of Chinese reading, Wi-Fi and
sleep recovery, and five OTA checks/font-list loads from each of BLE-off and
connected starting states. Do not install an OTA image during detection tests.
Cover WenKai 16/18 and Noto, SD-direct and Flash-cached fonts, cold/hot chapter
caches, and TXT/XTC lifecycle regression. Always verify physical buttons/menu
response as well as HID delivery. Require at least 512 B task stack headroom,
no font-selection loss, crashes or sustained decline in equivalent recovered
heap states. Keep any normal-reading gate failure open as a stability issue;
do not lower thresholds to pass. Enabling compilation in every build does not
complete this hardware acceptance matrix. X4 Flash JEDEC 85:2018 suspend support
was not confirmed by the driver, so automatic Flash erase/write suspend remains
disabled.
