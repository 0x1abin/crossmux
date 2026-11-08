# Metalio E-Ink4-Plus V1.4 / ESP32-S31

This is an experimental integration, separate from the S3/SPI `metalio_eink4`
target. The input-startup correction has now been flashed and verified. The
user accepted book-cover touch/open, volume+/volume- page turns, the three
cover keys including Home, normal text/orientation and repeated page turns.

**Current device:** corrected CrossMux is retained under the user's explicit
2026-11-08 instruction to debug first and restore only when requested. Existing
Flash/SD backups are retained; the active fresh recovery manifest is in parent
`backups/reader-runs/20261108-163445/manifest.json`. Capture termination does not
restore firmware or SD data. Automatic sleep remains Never during controlled
debugging. Three manual sleep/wake cycles now recover SD and reading correctly;
USB debug re-enumeration and measured power remain pending. Do not perform SD
rollback as a prerequisite to further debugging.

The first complete-reader trial reached native IDF startup but faulted before
`app_main`: `UITheme::instance` logged during global construction, before
Arduino's `HWCDCSerial` constructor. The matching ELF resolves the load fault
to `Print::write`, with a null vtable read at address 0x0c. No reader screen or
filesystem acceptance was obtained. The full ThingsOS app was restored and
verified; NVS/otadata/phy and immutable Flash remained unchanged. The user
confirmed normal display/touch/page turns, and a read-only SD comparison found
no reader-data changes. Recovery evidence is in parent
`logs/reader-first-trial-runtime-analysis.json`.

The Plus profile now selects the existing ROM log transport, matching its native
IDF USB Serial/JTAG console and avoiding access to Arduino's object in global
constructors. Other boards retain their existing policy, with C3 and ReadPico
profiles checked explicitly. A host harness compiles production `Logging.cpp`
and the real board profile, checking early ROM output, no Serial access and the
crash ring. The next physical trial reached InxRecent without panic, mounted
SD, discovered/loaded fonts and rebuilt an EPUB index/cover. The user saw the
interface but reported no touch/open response. The input backend was not started:
HAL deferred Plus input while the post-display call was gated on ReadPico only.
The call now includes Plus. An actual-ELF regression rejected the previous image
and verifies `display -> input -> renderer` in the new one. The corrected reader subsequently passed the user-operated touch,
navigation and EPUB page checks listed above.

The ROM-log trial produced 28 heap samples: free heap 164,907..165,215 bytes,
historical minimum 85,380 and largest block 65,536; PSRAM free was
14,928,708..14,935,968 bytes. Nine display timing samples were captured without
panic. This is bounded startup/idle/menu evidence, not a reading/sleep stress
test. Evidence: parent `logs/reader-romlog-trial-runtime-analysis.json`.

The connected V1.4 unit has ESP32-S31 rev 0.0, 16MiB Flash and 16MiB Octal
PSRAM. A standalone native-IDF 6.1 probe passed a 64KiB memory sample at 200MHz,
I2C address checks and the vendor display first frame. The user confirmed the
portrait test image and normal ThingsOS display/touch/page turns after restore.
A later native diagnostic measured five ordered touch targets, three cover
keys and active-low volume+/volume-/power levels, and passed eight read-only
SD sector0 checks in 4-bit/20MHz mode. ThingsOS was restored and physically
accepted again. This does not establish full CrossMux runtime, grayscale,
mounted SD filesystems or sleep; BOOT press also remains untested.

## Current source build

The complete Arduino/IDF source build passed with **200MHz octal PSRAM**,
DIO/80MHz 16MiB Flash and a 64KiB internal allocation reserve. Arduino is an
IDF component (`framework = arduino, espidf`), so the application and libraries
share native IDF 6.1 headers and sources. The link map contains the rebuilt
`esp-idf/esp_psram/libesp_psram.a` and no prebuilt Arduino IDF archives.

The native input/SD increment builds the complete reader with FT6336U/TCA and
4-bit SDMMC enabled provisionally. The original 200MHz build-only artifact was
6,540,320 bytes. The current input-startup image is 6,588,864 bytes (6.28MiB),
linked Flash usage 6,588,006 / 11,403,264 bytes (57.8%) and static RAM 107,736 bytes.
SHA-256: `5a32cbfd9bdf5349e49259825b7cd4c0c13142480ddcb2e17696f57ff4f8252c`.
Its artifact checks and current source fingerprints are recorded in the parent
`crossmux-plus-input-startup` reports. Previous input/SD, NVS-guard and ROM-log
artifacts/fingerprints remain historical evidence. The latest artifact has been flashed and physically accepted for the basic
reader/input scope. The ROM-log samples above remain historical evidence;
current evidence is recorded in parent `logs/reader-input-startup-debug-runtime-analysis.json`.
Image checks confirm S31, DIO/80MHz, valid checksum/hash, the Plus board tag and
the development version. The generated partition binary matches the backup.

## Reproduce

```sh
git submodule update --init --recursive
pio run -e metalio_eink4_plus
python3 scripts/tests/test_metalio_plus_build_only.py
python3 scripts/tests/test_metalio_plus_core.py
python3 scripts/tests/test_metalio_plus_early_logging.py
python3 scripts/verify_metalio_plus_core.py sdkconfig.metalio_eink4_plus
# With the selected S31 toolchain's objdump on PATH (or pass --objdump):
python3 scripts/verify_metalio_plus_startup.py .pio/build/metalio_eink4_plus/firmware.elf
python3 freeink-sdk/libs/display/MetalioPlusEpd/test/host/run.py
python3 freeink-sdk/libs/display/MetalioPlusEpd/test/host/run_lcd_compat.py
python3 freeink-sdk/libs/hardware/BoardConfig/test/host/run_metalio_plus.py
python3 freeink-sdk/libs/book/ContentProtection/test/host/run_psa_keygen.py
```

Sources used during development: reader `0ab0af23f69cca73af7cf0143eafbfe9f5d6d648`,
SDK `d9a1304cd234b0536136302f629304b609f62b32` plus local changes, vendor BSP
`2b2c36ae58f32a9cb1a721d1000883a2693651a4`. The SDK gitlink remains unchanged.
The new `MetalioPlusEpd/vendor-source-manifest.json` maps 74 copied vendor files
to their original Git blobs. Integration and HAL adapters live outside `src/vendor`.

For this Mac, the parent `tools/pio-s31-200m` wrapper selects a separate local
cache under `toolchains/.platformio-s31-200m`; the previous packages and native
probe toolchain remain available. Exact managed-component versions and build
evidence are retained in the parent logs. No global tool installation is needed.

## Integration

- `FREEINK_DEVICE_METALIO_EINK4_PLUS` derives a separate `FREEINK_MCU_S31`.
  The profile has native 1216×684 geometry, a 103,968-byte 1bpp facade buffer in
  PSRAM, and its own firmware board tag `metalio_eink4_plus`.
- `MetalioPlusDriver` uses an external bus, then delegates synchronous B/W
  drawing to `MetalioPlusEpd`. With a valid committed baseline, Fast uses the
  existing DU waveform and Half uses GL16. Window/split calls still compose a
  complete frame; this does not add rectangular or asynchronous refresh support.
  Native grayscale capabilities remain disabled.
- Set facade bits expand to white (15) in the vendor's existing 4bpp PSRAM
  buffer; no additional native framebuffer or heap allocation is introduced by
  the wrapper. The vendor still allocates its render buffers/tasks and internal
  DMA structures. Peak reading/sleep memory headroom remains unmeasured.
- Each draw powers on, checks TPS readiness, updates, powers off and checks
  PWR_GOOD low. First/Full/invalidated updates retain the accepted physical clear
  and from-white GC16/25°C sequence. Ordinary differential updates reuse the
  vendor's committed PSRAM back plane without clearing. Sleep/resync or failed
  work forces full cleanup next time. Unconfirmed power-off inhibits further
  panel work until reboot. Vendor initialization is called once.
- Arduino RC1's prebuilt LCD HAL clock APIs take group 0, while native IDF6.1
  takes a device pointer. `LcdClockCompat.h` checks both exact signatures;
  native compilation also rejects a PSRAM speed other than 200MHz. The separate
  C translation unit preserves vendor blobs and the native device pointer.
- GCC15's default C23 conflicts with pinned QRCode declarations. A Plus-only
  build middleware selects C17 for actual C compilation environments; existing
  C++20 policy and other targets remain unchanged.
- IDF6 uses PSA RSA generation/export; the previous mbedTLS path remains selected
  wherever its public RSA header exists. Exports retain existing SPKI/PKCS#8
  wrapping. SHA-256 declarations moved to a private header; the pinned archive's
  five legacy symbols were verified, with declaration opt-in scoped to the shim.
- S31 GPIO hold/isolation calls follow APIs and Kconfig actually present in the
  selected SDK. Sleep behavior and current are still unverified.
- The reader uses C++20, explicitly after IDF's C++26 default; C17 middleware
  handles the pinned C dependencies. Native IDF core components retain their
  own language defaults. The native version define is injected before IDF
  clones application environments; Arduino-only targets retain per-file injection.
- The generated, ignored root CMake file selects S31 defaults only for S31.
  It preserves a subsequent C3/S3 rebuild's defaults, and is recreated after
  legacy HybridCompile removes it. Unknown user CMake files are not overwritten.
- Global LTO is disabled on this target to preserve the bootloader's non-LTO
  compilation/link contract; the app still fits its slot. Reader panic and
  validation wrappers are removed before IDF clones the bootloader environment,
  then added to the application link only. ELF checks confirm those scopes.
- The Plus-only `HalNvsPolicy.cpp` linker wrapper maps NVS NO_FREE_PAGES and
  NEW_VERSION_FOUND to INVALID_STATE after one real init call. This blocks
  Arduino's automatic erase fallback; successful init and unrelated errors pass
  through. Ordinary NVS clients can still write. The linked `initArduino` calls
  the wrapper; the bootloader and legacy targets do not gain it.

## Current runtime constraints

The user has returned. Basic reader/input and ordinary reboot/resume checks
passed on the corrected 200MHz image. CrossMux and SD changes are retained
until an explicit restoration request. Automatic sleep is Never. Manual
sleep/wake, frontlight, read-only battery display and bounded STA file transfer
have separate acceptance records below. Differential refresh is entering
physical acceptance; measured current, native grayscale and remaining optional
peripherals remain pending.

The generated bootloader, partition table and factory image
must never replace the current ThingsOS installation in this application-only
porting workflow. The merged factory image also writes boot metadata at 0xf000.

The previous prebuilt-250MHz build remains a historical build-only artifact.
HybridCompile was rejected for this increment because it copied new archives
but retained older IDF headers. Native component compilation avoids that mixture.

`metalio_plus_build_only.py` allows only known build/metadata/host-test targets,
including PlatformIO's `__idedata`. It rejects upload, erase, `erase_upload`,
`__debug`, device tests and unknown custom targets before their actions run.

`partitions_metalio_plus.csv` describes the existing ThingsOS layout, with
the application at 0x220000 (0xae0000 bytes). OTA/release registration is absent.
The current corrected artifact has not been written. Earlier native diagnostics
restored only the application and verified protected Flash, followed by user
checks. Full-reader trials use the separately approved fresh mutable-region
recovery contract and a verified SD backup.

## Validation and remaining work

The focused host suite passed 12/12 tests, including native input and S31/S3
SD block I/O, CMake transitions, native version injection and both LCD HAL contracts.
Formatting and Git whitespace
checks are recorded separately from compilation. Real PlatformIO programming
commands remain rejected by the build-only guard before device access.

New host checks compile the production display adapter/PanelDriver, actual board
profiles, PSA export/cleanup helper and firmware tag scanner. Failure injection
checks power/clear/draw/off transitions. Recording APIs do not prove hardware
waveforms, entropy or cryptographic mathematics.

On this Mac the current repository host suite passed 1526/1532 tests, with fixture portability failures in
WeReadConnection, ReadingUiRegressions, UiFontFallback, FileBrowserImages,
WebServerStartupFailure and FirmwareUpdateRefresh (Apple clang warnings under
`-Werror`). These also fail in the pinned unmodified source export. The old
ReadPico transaction harness additionally requires GNU `ld --wrap=calloc`, which
Apple's linker does not provide. Neither set is counted as passing.

The native-target strict cppcheck run failed with 91 high, 4 medium and 12 low
reports in IDF headers. Its `check_skip_packages` filter uses the current cache's
path prefix: it omits Arduino headers but retains 249 IDF include directories
canonicalized to the reused older cache. That incomplete include graph triggers
`semphr.h`'s missing-FreeRTOS-header error and prevents a valid full analysis.
This run is inconclusive for reader defects and is not counted as passing.
A controlled `src/main.cpp` comparison, changing only include-path scope,
reproduces that preprocessor error with the partial graph and removes it when
both package roots are excluded consistently; this is only a diagnostic sample.
The earlier prebuilt-target analysis additionally reported one low-priority
`arithOperationsOnVoidPointer` message in unchanged
`lib/Epub/Epub/FootnoteEntry.h:94`, reproduced on the pinned unmodified header.
No diagnostic has been suppressed for this port. Strict cppcheck/full CI remain
non-green; analyzer package-path handling needs a separate repair.

Before a CrossMux hardware test:

1. Use the separately prepared full-reader transaction, rather than the native
   diagnostic's app-only restore. Automatic NVS erase is now guarded, but normal
   NVS clients, PHY calibration and possible boot metadata still require fresh
   backups of 0x9000..0x12000 immediately before the trial. The independent host
   runner prepares private, hashed snapshots and restores changed mutable regions.
   It rejects old/completed transactions, keeps an active recovery marker after
   failure, and continues other restore attempts after report/log I/O failures.
   Its 50 host checks cover recovery, CLI policy gates, USB and SD backup/rollback.
   Keep mode retains firmware/configuration and its active recovery record on
   capture end or interrupt; explicit restoration still uses the first fresh
   original snapshots. Do not prepare over an active debug transaction.
   Actual full-reader recovery restored/verified changed NVS plus the full app;
   unchanged otadata/phy were verified without writes. Immutable checks passed.
2. Establish one owner/lock for the vendor native I2C bus and other peripheral
   clients. FT6336U/TCA now use the vendor's native I2C1 facade and deferred
   startup; the subsequent frontlight/native read-only gauge increments are
   accepted in the bounded scope recorded below. RTC, IMU, audio and BLE remain
   disabled. No generic
   Wire client may create a competing bus.
3. Carry the measured touch axes, strip geometry and active-low TCA keys into
   full-reader acceptance; measure BOOT61 press, then validate mounted SD and
   exercise ordinary EPUB pages, touch edges,
   repeated refreshes, grayscale transitions and failure recovery.
4. Verify the complete overwritten application range and all actual mutable
   regions in a concrete write/restore plan, followed by physical acceptance.
   The current image exceeds the previously extracted short ThingsOS app file;
   restoring only that short file would leave a changed tail. A complete original
   application-slot restore file has been prepared and hashed locally.
   The user selected the existing microSD with backup first. The 15,938,355,200-byte
   external card was identified and remounted read-only. A private backup of 441
   files / 321,774,690 bytes, including hidden reader data and empty directories,
   passed source stability, size and SHA-256 verification. Volume identity was
   checked before and after copying. Only the protected macOS `.Spotlight-V100`
   index was explicitly excluded; an unreadable ordinary directory still fails.
   Parent `logs/sd-card-backup-result.json` records the manifest and its digest.
   The initial full reader-data comparison has no differences. The explicit SD
   rollback path verifies the same UUID/capacity, backup integrity and reviewed
   contents before native rsync, then verifies file hashes and tree state again.
   macOS Spotlight/FSEvents directories are preserved during rollback. These are
   file backups, not a raw card-sector image. Actual rollback on the card remains
   deferred by the user; only temporary-directory restoration was tested. The
   user now authorizes persistent debugging and requests restoration only when
   explicitly asked. SD rollback is not a prerequisite to further debugging.
   Verify sleep/wake and measured current separately.
5. Complete existing C3/S3 firmware regression builds/full CI on their pinned
   platform. Platform 55.03.311 is absent from the isolated caches, so the full
   default/legacy matrix was not started or installed implicitly. Cryptographic
   and networking runtime acceptance remains pending.

The exact peripheral transport, reset, key-strip and SDMMC prerequisites are in
[`metalio-plus-peripherals.md`](metalio-plus-peripherals.md). Input and storage
are present in the experimental profile. Diagnostic transport/raw-coordinate/TCA
and SD-sector evidence is recorded there. Basic full-reader/input and ordinary
reboot/resume have been physically accepted; broader power/stress work remains.
The input driver cancels screen/strip crossing, malformed/multiple contacts and
read failures. Canceled screen contacts retain the UI cleanup release without a
tap/swipe; canceled strip/native-button input cannot trigger release-driven
navigation. Activity suppression also clears capacitive levels. SD uses the
existing 4KiB buffer with S31 cache alignment and internal DMA capabilities;
there is no new input heap allocation or additional SD bounce buffer.

Affected SDK host harnesses passed 13/14 commands before final input fixes;
all six existing InputManager runners passed again after those fixes. The old
Metalio audio-manager ownership assertion also fails identically in the pinned
unmodified SDK export (`test_metalio_managers.cpp:24`); it remains a failure.
Plus audio stays disabled. The new input and SD tests passed in the final reader
suite, including forced touch-disabled production compilation.

Development logs, exact image digests and physical acceptance records live in the
parent independent directory; no Git commit, push, PR or release was requested.

The repeated-reset investigation also captured a rescue-image checksum failure.
Offline parsing of the **initial pre-port full backup** confirms stored checksum
0x59 versus calculated 0x55 for the image at 0x20000. This condition therefore
predates the diagnostic; protected-region comparisons show the rescue bytes
were preserved. Do not rely on that rescue image booting. The proven recovery
path is USB ROM/stub access plus the verified original app-slot restore file.
Evidence: parent `logs/thingsos-original-rescue-image-check.json`.

## Retained reader checkpoint, 2026-11-08

Parent `logs/reader-input-startup-debug-runtime-analysis.json` records the
corrected reader's physical acceptance and ordinary reboot. The initial capture
was intentionally interrupted after input/reading tests to perform the reboot;
keep mode retained the reader and active recovery marker. The candidate app
still matched, immutable Flash digests matched, and the restart performed no
Flash programming. The saved spine/offset/page matched the first reopened
position. The user confirmed that reading position, next-page operation and the
Never sleep setting survived. The second 180-second USB capture had no
disconnects, retries or panic markers. These are bounded observations, not
endurance or measured-current acceptance.

The Plus profile's direct power GPIO is unassigned; its real key is TCA P17.
The new Plus HAL branch uses the native checked sleep adapter and GPIO2 EXT1
instead of the generic direct-pin wake helper. The first deep wake exposed an
SD initialization-order error: the TCA retained rail-off latches, while SD
started before display initialization restored those rails. A keep-rails
control recovered reading. The final correction restores P14 then P5 through
the existing native I2C1 owner in `HalGPIO::begin()`, before SD starts; normal
sleep rail cut is retained. Other expander bits and deferred input startup are
preserved. The exact electrical SD rail has not been established by a V1.4
schematic; the control experiment establishes the software rail dependency.

The user accepted three corrected-image cycles for reading position, page turns
and Home. One complete log records deep-sleep reset=8, EXT1 wake=3 and SD ready
with 31,129,600 sectors. Later USB enumeration was unreliable and required a
cable replug; do not infer missing wake logs. A subsequent read-only Flash
check confirmed the current candidate, unchanged immutable regions and original
recovery association, then rebooted into the retained reader without writing.
Evidence: parent `logs/plus-early-rails-acceptance.json` and the associated
`postcheck.json`. Sixteen boot-rail and seventeen sleep host cases pass; the
full host run is 1,528/1,534 with the same six prior baseline failures. Static
RAM remains 107,760 bytes; linked Flash is 6,592,720 bytes. This establishes
bounded functional recovery, not measured sleep current or endurance.


## Frontlight and battery acceptance — 2026-11-08

The retained native-battery image adds capped GPIO6/7 cool/warm PWM and read-only
BQ27220 telemetry through native I2C1. The user accepted brightness, warmth and
on/off, displayed battery 97%, opening books/page turns, then one explicit light
sleep/wake cycle: light off asleep, page turns after wake, brightness/warmth
retained when the light is enabled. A captured gauge sample reports 97% and
4375mV with voltage/charging known; this is not independent SOC calibration.
See [peripheral details](metalio-plus-peripherals.md) and parent
`logs/plus-battery-acceptance.json` / `logs/plus-frontlight-sleep-acceptance.json`.

No charger or capacity-calibration writes were enabled. The original recovery
manifest remains associated; CrossMux/NVS/SD changes stay on device until an
explicit restoration request. Serial capture ended intentionally. Actual
sleep/active current, battery-only endurance, long-duration stability, networking
and remaining optional peripheral acceptance are pending. Intermittent USB
replug investigation remains deferred at the user's request.


## STA network/file-transfer acceptance — 2026-11-08

The retained native-battery image passed local-network WiFi/HTTP/mDNS access,
HTTP upload plus six hash-verified downloads, and the actual web page's 256KiB
WebSocket upload followed by a matching download. The user exited transfer,
opened the uploaded original test EPUB and accepted Chinese text, two page turns
and Home. No firmware change/reflash was needed. Only the generated EPUB remains
in `Metalio-Network-Test-20261108-215320`; own integrity TXT was removed. Serial
capture stopped without requesting a device reset or restoration. See parent
`logs/plus-network-acceptance.json` and the peripheral details document.

This does not establish AP mode, Calibre, internet TLS, endurance or measured
power. Remaining RTC integration must reuse native I2C1: the current generic
Rtc.cpp transport creates Wire and PCF8563 begin changes CLKOUT, so enabling the
capability/profile alone is not sufficient. Check the pinned board RTC wiring
and preserve unrelated RTC control/alarm state before a dependent increment.

## Differential refresh trial — 2026-11-08

The user prioritizes removing the full flash on every page turn. The original
Plus driver discarded the requested mode and always cleared before from-white
GC16. The correction routes Fast/DU and Half/GL16 through the existing checked
vendor differential update. Full, first frame and recovery retain physical
clear plus GC16. Both binary text and image pages now use the configured reader
cleanup cadence, with Plus periodic cleanup explicitly Full. Other targets keep
their existing cadence and image capability behavior.

No waveform, temperature, VCOM, rail timing, buffer, heap allocation or grayscale
capability changed. Fourteen production-driver host cases pass, including
baseline lineage and draw/power/off failures, sleep/resync, geometry and window/
split fallbacks. Actual reader helpers pass for Plus, ReadPico and X3, including
image pages. Existing display-facade tests pass. The final S31 build uses 107,968
bytes static RAM (unchanged) and 6,613,698 bytes linked Flash. All 74 frozen vendor
blobs and the SDK gitlink were verified unchanged. Full host CTest is 1,532/1,538;
the same six prior failures have the same compiler-error multiset.

The user confirmed markedly less flashing with clear text after repeated page
turns, then normal manual/menu refresh, Home/reopen and one sleep/wake/page-turn
sequence. Captured continuous reading cleaned at pages 9, 24 and 39, matching a
15-page interval. The sampled text-page driver duration had a median of 453ms
for DU versus 1,611ms for full cleanup; whole-page rendering is separately timed
and these are not button-to-photon measurements. All captured refreshes committed
successfully. Image-heavy optical quality and long-duration stress have not
been separately accepted; native grayscale remains disabled.

Parent `logs/crossmux-plus-differential-refresh-artifact-checks.json` records the
unique BIN/ELF/map and checks; `logs/plus-differential-refresh-acceptance.json`
records physical feedback and bounded timing evidence. The verified candidate
is retained. Capture ended intentionally via SIGINT, leaving an expected
KeyboardInterrupt in its report. The original active recovery association and
restore-only-on-request policy remain in force.

## Interface density trial — 2026-11-08

The user reports small interface text and bottom navigation icons. Native
1216x684 drawing already maps one source bit per panel pixel; portrait layout
uses 684x1216. Only GfxRenderer's pre-begin defaults use the generic dimensions,
and begin() replaces all four geometry members with the live driver values.
This symptom is therefore addressed at UI density, not by changing panel size.

The Plus build now opts into the existing `CROSSMUX_UI_PROFILE_HIGH_DPI` preset.
It selects actual 12/14/16 interface font resources and 56px navigation icons,
with the preset's row, header, gap and touch metrics. These are existing native
assets, not enlarged low-resolution bitmaps. The reading-family/size setting
remains independent. No SDK, waveform, grayscale or allocation-policy changes
were made. An independent focused review found no actionable issues, including
non-ReadPico font fallback and footer/cache behavior. Physical feedback below
confirms the current footer and content layout.

The S31 build passes at 108,032 bytes static RAM (+64) and 6,509,170 bytes linked
Flash. Font resource checks pass (11 cases, one optional regeneration skip), as
do the three selected high-density layout cases. The separate existing chrome
suite has five passes, one optional regeneration skip and one Apple-clang
fixture failure: unused `SMALL_FONT_ID` under ReadPico with `-Werror`. That
harness does not read the changed PlatformIO flags; its inputs are unchanged.
Parent `logs/crossmux-plus-ui-density-artifact-checks.json` records the unique
artifact and full checks. The controlled update wrote and verified the image,
retained the original recovery association and resumed reading with normal SD
access. The user then accepted font/icon size, normal display/touch and the
requested menu/Home/reopen/two-page reading sequence. Captured refreshes all
committed, with no captured panic or error markers. This is a bounded current-UI
check, not all-theme/orientation or endurance acceptance. The image remains on
device; capture ended intentionally. Parent `logs/plus-ui-density-acceptance.json`
records feedback and memory samples. No automatic commit or restore was made.
