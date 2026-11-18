# Metalio E-Ink4-Plus V1.4 / ESP32-S31

For the current accepted scope, prioritized remaining work and pending SDK
integration, see the [adaptation checklist](metalio-eink4-plus-todo.md). The
sections below retain the chronological development and acceptance evidence.

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
# Temporary SDK checkout while its integration PR is pending:
git submodule update --init --recursive
git -C freeink-sdk fetch https://github.com/lfxyl/freeink-sdk.git codex/metalio-eink4-plus-s31
git -C freeink-sdk checkout --detach 3cb3d568e3fe5c9d69d1ba47eb8dfdb64d34610d
git -C freeink-sdk submodule update --init --recursive
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
`2b2c36ae58f32a9cb1a721d1000883a2693651a4`. These are the original development
baselines. The tested SDK changes are now committed separately; while their PR
is pending, follow the [temporary SDK checkout steps](metalio-eink4-plus-todo.md#复现与依赖状态)
before running the commands above. The default gitlink retains the original SDK baseline.
The new `MetalioPlusEpd/vendor-source-manifest.json` maps 74 copied vendor files
to their original Git blobs. Integration and HAL adapters live outside `src/vendor`.

The local `tools/pio-s31-200m` wrapper in the separate development directory
selects an isolated cache under `toolchains/.platformio-s31-200m`. The checkout
has moved independently of that directory. Set `METALIO_DEV_ROOT` to the existing
development directory and invoke `"$METALIO_DEV_ROOT/tools/pio-s31-200m" run -d
"$PWD" -e metalio_eink4_plus` from the repository root. Previous packages,
the native probe toolchain, exact managed-component versions, backups and build
logs remain in that external directory. Historical references below to parent
artifacts/logs mean that development directory, not the checkout's new parent.
No global tool installation is needed.

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

## Four-level grayscale and text AA candidate — 2026-11-10

The user chose staged delivery and delegated the details: four-level grayscale
and AA first, native sixteen-level images/four-bit glyphs later. The native
front buffer now stages B/W plus ordered overlay masks (dark=11, light=01),
mapped to levels 0/5/10/15. The existing vendor back plane remains the completed
physical baseline. This introduces no driver framebuffer allocation or vendor
source/waveform/calibration change. Combined strip uploads avoid a separate B/W
activation on ordinary AA/image turns; GL16 carries the grays and existing
Full cleanup/recovery keeps GC16. The first B/W return uses GL16, then ordinary
B/W continues using DU. EPUB explicitly honors Combined for image strips and
binary reading backgrounds; XTC uses the shared periodic Full decision.

The driver validates complete, ordered plane coverage and masks before commit.
Cancel leaves the glass unchanged; unsubmitted OOM fallback can finish B/W,
while failed hardware work is not retried by cleanup. Gray cleanup preserves
the actual gray baseline rather than replacing it with a B/W proxy. Existing
reader strip scratch is 12,160 bytes; whole-plane compatibility retains its
existing snapshot behavior. Legacy AirPage/Standby/Buddy callers that omit
`displayGrayscaleBase` remain B/W on Plus and are outside this acceptance round.

The real-driver harness passed 34 cases (20 added plus the original 14), after
the capability test first failed before implementation. The profile first
rejected two versus four levels, then passed with the C3/ReadPico guards and
reader cadence checks. All 74 vendor blobs remain unchanged. A fresh relocated
host build passed; full CTest remains 1,532/1,538 with the same six baseline
failures and identical compiler-error multiset. Independent review found no
actionable correctness issue; actual EPUB image/background and XTC branch
integration plus optical behavior remain physical acceptance items. Legacy
firmware matrix/full CI limitations recorded above still apply.

Native S31 compilation passed with 108,040 static RAM bytes (+8 from the accepted
UI image) and 6,510,008 linked Flash bytes. The candidate BIN is 6,510,864 bytes,
SHA-256 `6c7e9b786f8d52d9301fefadf69188d6c16c2aad881bb4fe6dfc2d41798244aa`.
Native 200MHz config, compiled startup order, image checksum/digest/board/slot,
original partition bytes and recovery-manifest integrity checks passed.
External `logs/crossmux-plus-four-gray-artifact-checks.json` and the separate
`artifacts/source-snapshots/plus-four-gray-20261110` preserve this uncommitted
source/candidate combination. The main gitlink still retains the configured
upstream SDK baseline; these checks use the temporary feature SDK checkout.

The user is away and requested batching manual tests on return. **This candidate
has not been flashed; grayscale/AA physical acceptance is pending.** The accepted
UI firmware remains on device. Later independent adaptation work may continue,
with candidates and source evidence recorded separately; no automatic restore,
commit, push or PR-readiness claim follows from this build.

## Native RTC candidate — 2026-11-10

The matching V1.4 vendor config places PCF8563@0x51 on I2C1 SDA0/SCL1 at
100kHz. Plus boot rail setup already initializes that native bus before
HalClock::begin; the RTC now reuses its owner/mutex and does not create Wire1
or reorder startup. Begin is read-only. One nine-byte transfer captures control0
and calendar state; STOP/test/VL, malformed BCD and impossible 2000–2099 dates
are rejected without modifying the output. Trusted set holds one mutex across
STOP/calendar/restart, preserves alarm/CLKOUT/timer registers and leaves a failed
partial calendar stopped. Reserved control0 bits are written zero, following
[NXP PCF8563 Rev11.1](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf).
RTC alarm wake is not enabled; INT is on TCA P16 rather than an ESP GPIO.

The profile now enables RTC only; IMU/environment remain disabled. No new
application buffers or tasks were added. The existing native owner lazily
allocates a cached IDF device handle for 0x51 on first access; that internal heap
cost has not been measured. HalClock's existing software-clock/trusted-floor
behavior stays in place. Previous ThingsOS/vendor UTC versus local-time encoding
is unknown: check and synchronize with trusted time before accepting offline time.

31 production RTC/native-I/O ASan/UBSan scenarios pass after the native-routing
assertion first failed. Existing RTC/PMIC (18/26 injected fault points), disabled
RTC and 22 Plus battery cases pass. The original PCF85063 host script fails on
its missing I2cBusLock include path; with a host-only BoardConfig include path
appended after its own profile stub and unused-const warning demotion, its original
roundtrip/year/probe/error assertions pass. Both strict failure and adapted pass
logs are preserved. Plus/C3/ReadPico profile guards pass. Full CTest is now
1,533/1,539 with the same six prior failures and identical 56 compiler errors.
Independent review found no actionable correctness issue. No complete CI claim follows.

The combined RTC/four-gray S31 build passes at 108,048 static RAM bytes (+16
from accepted UI), 6,511,994 linked Flash bytes. BIN 6,512,848 bytes, SHA-256
`8e808c5f7b5095f278fad316f7cc3e830bc7db6bcc59e3f425a65ddd7643cb80`.
Native 200MHz config, compiled startup order, image checksum/digest/board/slot,
original partition bytes, frozen 74 vendor blobs and original recovery checks pass.
External `logs/crossmux-plus-rtc-four-gray-artifact-checks.json` and
`artifacts/source-snapshots/plus-rtc-four-gray-20261110` preserve the exact
uncommitted candidate. It has not been flashed. Clock continuity/timebase, native
I2C coexistence, runtime heap and optical acceptance remain pending.

Further inspection found SC7A20H INT1 is wired with TCA INT to GPIO2; the accepted
power-key wake path uses this line. Earlier read-only probe saw 0x19 respond, 0x18
absent, but did not validate silicon version or mount axes. Generic Imu currently
initializes Wire and rewrites IRQ configuration. It remains disabled until those
contracts are resolved; no unverified automatic rotation is exposed. USB MSC also
remains off: the current native build uses USB Serial/JTAG and has no TinyUSB
configuration. An OTG stack, pin/PHY ownership and exclusive SD ownership must be
validated before enabling the existing MSC facade.

See the [batch acceptance checklist](metalio-eink4-plus-acceptance.md). The user
requested skipping feedback-dependent steps while away. Accepted UI firmware,
Never auto-sleep setting and the original recovery association remain unchanged.

## Four-gray physical acceptance — 2026-11-12

The user returned and candidate `6c7e9b78…` was written/verified using the original
active recovery association. Immutable-region digests matched before the update.
The user accepted clearer/smoother text, 20 requested cumulative page turns,
menu/Home/reopen, four distinguishable black/dark/light/white patches in the new
EPUB fixture, ten additional fixture turns, AA off/on, 26 pt to an adjacent smaller
size and back, and one gray-page sleep/wake with progress/turn/Home intact.

The retained refresh interval was 30 pages. During a controlled temporary 5-page
interval the serial trace recorded successive Full commits at offsets 0/5/10/15,
with four GL16 commits between them. All those updates completed. Original 30-page
interval, AA enabled, 26 pt LXGWWenKai and Never sleep were verified via settings
afterward. Ordinary panel GL16 commits in the initial sample had median 754 ms;
this excludes rendering and is not a universal page-turn benchmark.

The fixture EPUB/PNG were uploaded into a new `Metalio-Gray-Test-20261112-*`
directory and downloaded with matching SHA-256. No original book was overwritten.
Logs/receipts remain external under `logs/plus-four-gray-physical-acceptance-20261112.json`
and `logs/plus-four-gray-fixture-upload-20261112.json`. Earlier capture also contains
a deliberately cancelled render during navigation, network-view clipping diagnostics,
disabled IMU and a duplicate-font diagnostic; these are not hidden as a clean global
error log. No physical display/operation defect was reported in the tested sequence.

This accepts the bounded EPUB four-gray/AA path, not all themes/directions, XTC,
custom reading backgrounds, old standalone gray callers or endurance. RTC remains
a separate acceptance stage; the initial register timebase was inspected before
activating it, as recorded below.

## RTC initial state and POR recovery — 2026-11-12

A standalone read-only probe took three nine-byte PCF8563 samples without NVS,
SD or calendar/control-value writes. Control0 was `08` (TESTC POR override), and
seconds had VL set in all samples. This is an uninitialized/untrusted calendar;
neither UTC nor a legacy local-time convention can be inferred. No initial time
was adopted. Raw evidence is external `logs/plus-rtc-initial-readonly-20261112.json`.

The earlier explicit-set guard rejected TESTC together with TEST1. That prevented
trusted initialization of this actual unit. Following NXP sections 8.3.1/8.11.1,
`MetalioPlusIo.cpp:113` now permits clearing TESTC only in the explicit trusted
STOP/calendar/run transaction. Read-only begin/now still reject STOP/TEST/VL, and
TEST1 external-clock mode still rejects writes. Alarm/timer registers are untouched.
The new recovery case failed against the old guard, then all 37 RTC ASan/UBSan
cases passed, including each failure point with TESTC/VL initially set. All 17
focused Plus CTest entries pass. No full-suite or CI-green claim follows.
The current `./bin/ci-check` attempt passed formatting, then remained in
PlatformIO's default-target check preparation without cppcheck diagnostics for
over two minutes and was interrupted. Its incomplete output is retained as
`logs/plus-rtc-por-ci-check-20261112.log`; this does not establish a static-analysis
pass or a new source failure. The previously recorded full-suite limitations remain.

The corrected combined build uses 108,048 static RAM bytes and 6,511,992 linked
Flash bytes; BIN 6,512,848 bytes, SHA-256
`06a27c6e3125a99ff0cd56350c36f21b8b6ad7d7f6eaab8a7e54d33c257f037e`.
200 MHz config, compiled startup order, image/partition/recovery checks and all
74 frozen vendor blobs pass. Exact source and artifact hashes are external in
`artifacts/source-snapshots/plus-rtc-por-four-gray-20261112` and
`logs/crossmux-plus-rtc-por-four-gray-artifact-checks-20261112.json`.
Controlled app-only update `20261112-220425` verified this image; all three
immutable-region digests matched before writing, and the original recovery
association remains intact. Startup capture missed early clock messages because
of USB settling, so that initial capture did not establish RTC discovery. Later
discovery/sync/sleep evidence is recorded below; isolated heap cost remains unmeasured.

The user subsequently reported successful manual sync with correct local time.
The initial 900-second serial capture had already ended before that operation;
external write/readback success was not inferred from the UI alone. On exit the
user reported no reboot and correct time. Plus follows the existing touch-device
`finishWifiSessionWithoutRestart()` path: it stops Wi-Fi without a reset. No
reboot acceptance follows from this exit.

A host-controlled ordinary reset rechecked the exact candidate and immutable
digests. Its boot log found the external RTC and logged local system time 22:50.
The `RTC clock read` label in main actually reads HalClock's system time, which
can survive an ordinary reset; it does not independently establish external RTC
restore. The existing standalone read-only probe was therefore run again. It
read `control0=00`, VL clear and UTC `2026-11-12 14:52:07 / :09 / :11`, all within
the host capture window. With no NTP/calendar/NVS/SD writes in the probe, this
independently confirms trusted sync reached the hardware, UTC storage is correct
and the calendar advances. Reserved weekday bits vary; the decoded weekday was
4 in all three samples. Probe update `20261112-225143` and external
`logs/plus-rtc-trusted-readback-20261112.json` preserve these readings.
The same combined RTC/gray reader was resumed and verified in update
`20261112-225415`. Subsequent capture recorded the firmware's own successful
write/readback `External RTC set to 2026-11-12 14:58:33 UTC` followed by system
sync completion. Wi-Fi was stopped before the manual sleep. The user accepted
one sleep/wake with time, position, text, turns, Home and frontlight normal.
The requested sleep was about one minute; actual duration was not measured.
Serial confirms deep sleep, `reset=8 wake=3`, SD ready/err=0 and nine completed
refresh commits afterward, without post-sleep network/NTP. Post-wake idle samples
were stable at 158,383 internal free bytes and 13,891,316 PSRAM free bytes; this
is a bounded sample, not an isolated RTC allocation cost or endurance measurement.

The user explicitly skipped the additional approximately ten-minute sleep test;
only one round is counted, and the longer test remains deferred. Drift, backup
supply/total power-loss retention, actual RTC-register retention after sleep and
an explicit invalid-system-clock fallback test remain pending. Evidence is external
`logs/plus-rtc-physical-acceptance-20261112.json` and
`logs/20261112-225644-plus-rtc-sleep-serial.log`. The read-only capture was stopped
after recording acceptance. CrossMux remains on device, auto sleep remains Never,
and the physical acceptance itself made no commit or push.

A focused read-only review of the final POR recovery/native RTC code found no
actionable correctness issues. It checked the NXP TESTC/TEST1 distinction and the
partial-calendar failure model, and inspected the existing 37-case and 17/17 logs;
it did not rerun builds or access hardware. This does not change the outstanding
full-CI or SDK-integration limits.

## Local feature commits and precommit checks — 2026-11-12

At the user's request the changes were separated by feature. SDK grayscale is
`e0a63f5`, and native RTC is `74ed189d55fa315073c5dbe5fe74d455546d7f1e`.
CrossMux reader integration is `76460d6e`, and RTC test registration/design is
`cb71b7e5`. Acceptance/backlog documentation is a separate commit. These new
commits have not been pushed. The committed main gitlink stays at the configured
upstream baseline `d9a1304c…`; the local SDK checkout intentionally differs until
upstream integration. Published base-port reproduction commands do not yet fetch
the local grayscale/RTC commits.

Fresh precommit checks passed all 17 Plus CTest entries, existing RTC/PMIC and
disabled-RTC cases, display bus/Pro regressions and changed-line formatting.
The grayscale-only staged profile was also compiled independently for Plus,
shared X3/X4 and ReadPico before committing, with mixed S31/S3 rejected.
Whole-file RTC formatting still flags two unchanged baseline lines; the original
SDK HEAD has the same diagnostics. No unrelated formatting cleanup was included.

The new native build attempt stopped in the `tool-esp_install` IDF tool setup
with rc=1 before compilation, so it is incomplete. All 18 relevant source/test
files match the previously built and physically tested candidate's source
snapshot. This preserves the earlier build evidence without claiming that the
fresh attempt passed. External logs are `logs/plus-precommit-*-20261112.log` and
`logs/plus-precommit-source-identity-20261112.json`. No new device flash or
hardware acceptance was performed as part of these commits.

## Unattended IMU/rotation software increment — 2026-11-13

The user ordered IMU/rotation, native sixteen-gray, cover/image gray, then build
repair, with physical acceptance postponed until return. Device firmware/settings,
SD and the recovery association remain untouched. No implicit commit/push follows.

The Plus profile now describes SC7A20H@0x19 on native I2C1. Production Imu routes
through MetalioPlusIo instead of Wire, validates WHO11/VERSION28 and compatible
polling configuration, and enters standby. Wake/read/sleep use the existing
owner/mutex; no IRQ/reset/FIFO/filter routing is rewritten. Unknown identity or
incompatible shared-line configuration is refused. The native owner lazily adds
one IDF device handle; its heap cost remains unmeasured. No new application task
or dynamic buffer was added. Other device paths retain their transport.

Plus HAL shares a sample between tilt and an allocation-free four-pose classifier,
with 600ms dwell, 1200ms cooldown and rejection of flat/noisy/invalid gravity.
The persisted, translated reader-only `autoRotate` toggle defaults off. Popups,
render work and held/edge/touch gestures gate rotation; EPUB reuses its locked
content-offset/reflow path. XTC reuses manual rotation. Mount flags are provisional,
not bench-accepted. No behavior is claimed on this actual IMU until return.

Native routing first failed against the competing Wire1 path; all 33 production
ASan/UBSan IMU cases now pass, including partial-transfer failure and preserved
IRQ state. Real Plus/C3/ReadPico profiles, existing SC7A20H/BMI270/gyro suites,
37 native RTC cases and Plus battery regressions pass. Classifier/HAL admission
checks pass, Chinese settings/i18n checks pass, and settings API keys remain
unique across 128 capability combinations. Full host CTest is 1,536/1,542: the
same six baseline targets fail with the identical multiset of 56 compiler errors.
No new firmware build is claimed; environment repair is deliberately the last
requested subsystem. Logs use external `logs/plus-imu-*-20261113.log`, with RED
and GREEN evidence also under `plus-hal-rotation-*` and `plus-auto-rotation-*`.

### 2026-11-13 原生十六级软件候选（未刷机）

Plus 原生目标使用既有 415,872 字节前缓冲，保留 16 档数值；普通灰阶 GL16，首次/周期/手动全刷 GC16。刷新计数仅成功后发布，硬件失败不立即补发黑白。主机 1536/1542，通过项之外为既有六个基线失败；原生渲染、46 项显示驱动用例及旧驱动门面兼容测试通过。手动全刷请求在原生提交中一次消费。图案已生成，实体十六级和周期清理验收待用户返回。

### 2026-11-13 封面与图片灰阶候选（未刷机）

PNG/JPEG 原生解码写入同一十六级目标，EPUB 原生图页绕过仅能存四档的 PXC。AA 关闭时保留黑白文字；两位字体可在原生图页保留四档覆盖度。背景图、假粗体仍走既有四级路径。GIF 暂保留四级量化，透明休眠叠加仍走既有合成；不宣称这些路径已有原生十六级质量。

Plus EPUB/TXT 封面使用独立 `cover_gray8*`、`thumb_gray8_*` 标准8位 BMP，旧缓存不覆盖；旧 EPUB 书架路径在读取时映射，XTC 路径不变。普通书架/最近阅读/书库灰阶帧完整重绘，取消只保留黑白的封面区域快捷缓存。查看器 PNG/BMP/JPEG 和普通休眠封面接入灰阶；原生硬件提交失败不立即补发另一波形。

资源：借用原有415872字节前缓冲，未新增全屏缓冲或任务。BMP 使用已有行缓冲扩为 Gray8，每行最多2048字节灰度加既有输入行；PNG保留既有解码/行缓存。灰阶BMP比旧1位缩略图增加磁盘用量，编码8位约为1位的8倍（另含BMP头/调色板），按需生成。主机检查不能代替真实屏幕残影、色阶、旋转和休眠验收。

### 2026-11-13 编译恢复与最终软件检查

已恢复独立 S31 编译：GCC15.2.0_20251204、Arduino4.0.0-RC1/IDF6.1.0、esptool5.4.0、SCons4.11.1。原生 `tools` 根目录及关键 packages 曾链接到共用 core，被其他目标安装自检改写；现改为独立副本，保留原链接/元数据，缓存包哈希核对。S31包装器只接受Plus环境，省略环境时默认Plus；C3使用独立的常规core。C3常规platform55与新版PIO Core对SCons版本要求不一致，机器本地元数据和忽略的local.ini固定到已有完整4.11.1/esptool5.4缓存；未修改仓库发布的依赖版本。

审查后S31构建通过：静态RAM108080字节，链接Flash6518596字节；C3default构建通过：RAM68144字节，Flash6371675字节。随后S31共存复编通过。主机1536/1542，与原六个失败及56条编译诊断完全一致；格式检查无诊断。严格PIO cppcheck仅保留未改动的`lib/Epub/Epub/FootnoteEntry.h:94`低优先级报告；未声称完整CI或其他S3矩阵通过。74个冻结vendor文件与SDK HEAD一致。

独立审查五项已修正：黑白图片选项不改变文字AA；两位字形可与16级图片同帧；输入/菜单/渲染拥有页面时取消旋转候选；相邻采样超过240ms重新计时（HAL80ms轮询）；原生BMP解码/行分配失败先取消目标，再显示既有翻译错误界面，硬件提交失败仍不立即重试。专项检查覆盖真实PNG/JPEG/字体/渲染、截断BMP和分配失败、HAL采样及输入准入。

新候选未写入设备，当前设备仍为上轮四级灰阶/RTC已验收固件。尚未提交或推送本轮改动，保持原SDK gitlink；见[返场统一验收](metalio-plus-return-acceptance-20261113.md)。

最终未刷机候选：`plus-imu-native16-images-20261113-032833/firmware.bin`，6519456字节，SHA-256 `ba851378de56c7d7882b533558899de0041d178ae50bef48d335193a447aafd8`。BIN/ELF/map/配置、源码快照和检查日志保留在独立开发目录；镜像离线校验为S31、DIO80MHz/16MB、octalPSRAM200MHz，校验和/摘要有效，位于既有0x220000应用槽大小范围内。没有写入设备、更新当前调试指针或覆盖任何SD内容。

## 2026-11-17 返场验收进展

Plus V1.4安装坐标实物校正为`(x,y)=(-rawY,-rawX)`，只调整板配置的交换/反向标志；正常、左/右横屏、倒置及回正停稳后方向正确，旋转后的触摸/音量翻页、Home及书架保持固定方向由用户确认。未直接采集原始三轴数值；平放抑制、IMU睡眠生命周期及功耗尚未实测。开关已关闭、竖屏。

校准固件BIN 6519472字节，SHA256 `1970a11f5cc3359b10125a25ab3a89b1b0b6ce44b2d54d9dfda4d5332542be8a`，仅原应用槽写入并校验；原恢复关联保留，按用户要求继续调试不还原。当前配置/进度/缓存178个文件已独立只读备份（二次下载比对，不是全SD镜像）。

16级首轮显示缺陷正在修复：启用伪粗体时为既有四级回退；关闭后EPUB PNG显示方框，串口证据是连续内存不足（最大25600、需要59456字节），原生重解码仍持有BW/PXC缓存。PNG查看器还出现预览宽2594超过Bitmap2048上限。详细证据和用户反馈见[返场记录](metalio-plus-return-acceptance-20261113.md)；此时不能标记原生十六级或封面图片实物通过。

### 2026-11-18 TTF后续（未刷入）

图片修复固件f33d8485…已由用户确认EPUB16档图及往返翻页、PNG/JPEG/24位BMP查看器和最近阅读/书库灰阶封面正常。改用TTF后用户报告页脚数字变化而正文似乎未更新，保持未通过；串口确认字体选择从26pt被退回22pt，并有一次LCD reason3欠载，次次完整恢复的结论尚不成立。已检测的那一次失败确实保留进度/Full债务，下一次GC16成功。原测试文案重复较多，不能据此单独解释全部反馈。

高密度矢量字号8–32的修复及可选源帧指纹已开发并构建为普通/诊断两个未刷入候选；不改vendor扫描时序，不新增堆/全屏缓冲或任务。实际TTF在22/26/32pt各32帧主机渲染及ASan/UBSan通过，完整主机1537/1543，六个既有失败和56条编译诊断不变；严格CI仍受FootnoteEntry.h:94基线报告阻挡。74个vendor文件与SDK HEAD一致。额外诊断遍历可能改变耗时/cache足迹，返场先用普通对照，再按需要使用日志，不能把诊断变体无复现当作修好。

新样本、候选哈希、保护流程和详细边界见[返场记录](metalio-plus-return-acceptance-20261113.md)。用户休息期间没有再次刷机、上传样本、修改设备设置或还原ThingsOS；新实机操作等用户返场。当前设备仍为f33d8485…，字号/字库取用户最后选择的TTF状态，原LXGWWenKai26pt/伪粗体标准设置已有备份，验收后再按用户需要恢复。


### 2026-11-18 晚 TTF不同正文实测与周期堆统计隔离

原f33d8485…固件上，两本独立原创书分别用不同章节标题和同章段落编号复测，22pt正文随翻页正常变化；旧样本大量重复文案会造成视觉歧义，但不能据此解释所有旧反馈。本轮仍捕获一次LCD reason3欠载，eof_gap545us；与昨晚719us事件均紧邻十秒堆统计。固定IDF的最大块查询在临界区遍历TLSF，主循环和渲染同为核1，存在阻塞LCD中断的机制。

周期内存/BLE统计改为只在既有Try RenderLock成功时采样，忙时保留到期状态到下次空闲，不新增堆分配、任务或帧缓冲，不改厂商扫描时序。包含TTF26/32pt字号修正的普通候选c5408611468ba7187a71cb0ca5e593b62e4d2ec23e2d912cd1282de14b322fbd（6519552字节）已写入应用槽并独立校验，原恢复关联保留。最新NVS/otadata/PHY已备份，刷入前当前应用及保护区摘要正常。

用户已确认22pt向后35页/回翻2页正文持续变化。阶段日志43次提交均成功、零欠载，周期GC16第7/37页覆盖30页清理。随后26pt明显变大，正文与菜单正常；Home重开、一次短休眠/唤醒、两页翻页及Home也通过。最终81次提交均成功、零已检测欠载；只读终检的当前应用、原分区与三段保护区摘要均正常。此为有界验收，未覆盖所有字体/模式与长期可靠性。主机1538/1544，六个既有失败保留；S31/C3构建、针对性六项CTest和格式通过，严格CI仍停在FootnoteEntry.h:94基线问题。详细步骤和后续结果见[返场验收](metalio-plus-return-acceptance-20261113.md)。


### 2026-11-18 本地提交状态

本轮功能和验收文档按关注点分别本地提交。SDK当前HEAD为 `15469204352b29ce2606c780919036046242a2b7`，主仓库仍保留原gitlink；未推送、更新远端PR或再次刷机。提交前7项针对性CTest及SDK IMU/显示/门面/板配置检查通过，完整CI的既有阻挡保留。当前建议先验收自动休眠，再验收休眠封面灰阶；具体待开发、待实测和发布条件以[剩余适配清单](metalio-eink4-plus-todo.md)为准。
