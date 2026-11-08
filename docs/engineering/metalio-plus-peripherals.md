# Metalio Plus V1.4 peripheral integration contract

Source-backed requirements and current experimental integration. FT6336U and
4-bit SDMMC are now compiled into the Plus profile. Their transport/gesture/DMA
host checks pass. The native diagnostic now supports the existing touch-axis
mapping, strip positions, active-low TCA keys and repeated raw SD reads on the
connected unit. The complete corrected reader now opens EPUBs and navigates
with touch, both volume keys and the cover keys; repeated turns and Home were
physically accepted. Three manual sleep/wake cycles also recover reading and SD;
USB auto-enumeration, measured power, BOOT press and broader stress remain pending.
CrossMux is retained on the device until the user explicitly requests restoration. Paths below are relative to this reader checkout;
vendor reference paths are in the parent independent development directory.

## One native I2C owner

`MetalioPlusEpd/src/vendor/bus/metalio_i2c1.h:14-31` exposes the bus mutex and
synchronous transactions. Its `.c:138-163` creates I2C1 on GPIO0/1 and refuses
different pins after initialization. Device handles are cached in 16 static
slots; each successful new handle still allocates inside IDF. Transactions use
100kHz (`.c:202-209`). Preserve this owner and frequency before measuring changes.

Display TPS65185 and TCA9555 already use it. A board adapter must acquire
`metalio_i2c1_lock(timeout_ms)`, call the appropriate `*_locked` operation and
unlock on every exit. The mutex is non-recursive. Do not hold it across reset
delays or call higher-level TCA helpers while holding it. The vendor ordering is
shared-reset lock -> output-state lock -> I2C; preserve that ordering.

Do not enable the generic FT6336U Wire path unchanged:
`InputManager/src/InputManager.cpp:3708,3743,3784` creates and uses Arduino Wire
without this owner. Battery/RTC/IMU clients likewise need native transport before
their profile fields are enabled, regardless of whether the chips ACK.

## Touch and buttons

- FT6336U is at 0x38, INT GPIO5. Reset is **TCA P12, expander index 10**,
  not ESP GPIO12 (which drives panel data). See vendor `config.h:96-102`.
- Board API `epd_board_metalio_eink4_plus_tp_reset()` owns the shared reset
  sequencing; P12 is also camera/4G reset. Optional users remain disabled, and
  reset should occur once during board startup rather than on read failures.
- Vendor touch reads 11 bytes from register 0x02, accepts down/contact events,
  and writes volatile interrupt mode 0xA4=0. No firmware upgrade is needed.
  See parent `sources/metalio-display-reference/.../display/metalio_touch.c:165-199,246-285`.
- Vendor coordinates contain a portrait/landscape heuristic (`:85-106`). Five
  ordered manual target taps on this unit report the displayed portrait axes,
  supporting the existing native mapping `(x,y) -> (y,683-x)`. Do not infer
  orientation separately from each sample or treat strip coordinates as taps.
- Key-strip reports are near raw Y=1600, X=100/200/300, tolerance 30, per vendor
  `config.h:105-112`. Ordered left/center/right taps reported exactly those three
  positions on this unit.
  Use InputManager's existing debounce machinery; preserve
  one gesture -> one action and keep a strip contact out of touch hit-testing.
- TCA P10/P11/P17 are volume+/volume-/power inputs (indices 8/9/15);
  Recorded idle/press/release samples confirm all three are active-low. BOOT is
  ESP GPIO61; only its idle-high level was measured. Read-port failure must not
  synthesize a press or retain a held key indefinitely.

Board initialization must establish native bus/TCA/rails before input. The
reader calls `gpio.begin()` before storage and display
(`src/main.cpp:923,981`), then calls `gpio.beginInput()` after display init
(`:690-698`). Use the existing deferred-input seam. Do not initialize the
non-idempotent vendor framebuffer/task stack merely to read a button.

Follow SDK `docs/adding-a-touch-controller.md`: update touch-device selection,
FT6336U driver flag and per-profile compile assertion together; compile real
InputManager in tests for press/move/release, failed reads, strip isolation,
rotation and held-key transitions.

## SDMMC

V1.4 uses dedicated slot 0: CLK24, CMD25, DAT0..3=20..23
(vendor `config.h:128-141`). Source IDF's `SDMMC_HOST_DEFAULT()` selects slot 0
for S31. Do not borrow the S3 slot or SPI profile. Vendor mount uses 4-bit mode
and no formatting on failure. GPIO holds must be released before slot setup.

The SDK's `SdmmcBlockDevice` preserves the FsFile/SdFat boundary. Its S31
alignment implementation follows parent
`sources/reference/metalio/.../common/storage/SdCardManager.cc:42-67`, which
queries the internal cache alignment, allocates an aligned DMA scratch buffer
and sets the host DMA-alignment callback. The implemented S31 path is in SDK
`SDCardManager/src/SdmmcBlockDevice.cpp`.
Any extra allocation must remain bounded, checked and explained; reuse the
existing 4KiB bounce buffer if possible.

Enable `FREEINK_SD_SDMMC` and `USE_BLOCK_DEVICE_INTERFACE=1` together. USB MSC
remains disabled; its current build hook does not automatically supply the
block-device define when USB MSC is off. First hardware validation should only
identify the card and read known sectors; mount/write/EPUB acceptance is separate.

## Startup writes before a full reader run

Arduino RC1 `cores/esp32/esp32-hal-misc.c:335-351` initializes default NVS and
erases its partition on NO_FREE_PAGES/NEW_VERSION_FOUND. The Plus-only
`lib/hal/HalNvsPolicy.cpp` now contains that fallback by returning INVALID_STATE
for those two errors; the application link hook adds the wrapper after IDF clones
the bootloader. Compiled production host tests and actual `initArduino`
disassembly verify the policy and link scope. It does not disable ordinary NVS
writes. Existing ThingsOS NVS begins at 0x9000; WiFi/PHY storage and boot metadata
also require coverage. A separate host reader runner prepares fresh private
NVS/otadata/phy snapshots and restores the full app plus changed mutable data.

Reader SD startup can load/migrate settings, rename staged files and create guide
or crash files. The user chose the existing microSD with backup first. The
independent backup tool copied 441 files / 321,774,690 bytes, including hidden
reader data and empty directories, from the same-capacity verified read-only
external card. Source and backup hashes passed; the protected macOS Spotlight
index was explicitly recorded as excluded. Initial reader-data comparison is
unchanged. Explicit rollback requires a fresh reviewed tree/content digest and
same-card identity; its native rsync path was tested on temporary directories,
not on this card. SD restoration is separate from Flash restoration. The reader
CLI requires both a verified SD backup and recorded full-trial authorization.
The user authorized and ran a temporary reader trial. Application/current NVS
restoration passed in that historical run. The user subsequently returned and
authorized persistent debugging: the corrected reader is now retained, with SD
changes and the active original recovery snapshots preserved. Use explicit
keep mode; capture termination must not trigger restoration. SD rollback is
deferred until requested. Keep the ordinary build programming guard enabled.

## Implemented in the native input/SD increment

`MetalioPlusIo` wraps the existing owner with 50ms bounded locked transfers,
one touch reset after board readiness, a volatile A4 mode write, 11-byte reads
from 02 and checked TCA port1 reads. It never initializes Wire, framebuffers or
another I2C mutex. InputManager's Plus branch polls at a 10ms floor and defers
until the existing post-display beginInput call. Reset failures require reboot;
ordinary read failures cancel input without resetting shared hardware.

The explicit sensor mapping is `(x,y) -> (y,683-x)` into native
landscape coordinates, with screen raw bounds x<684/y<1216. The vendor heuristic
is deliberately not used to infer orientation from individual samples. Strip
contacts at the documented positions map Home/Up/Down; BOOT61 maps Back. TCA
8/9/15 map active-low Up/Down/Power. The diagnostic measurements below support
these axes, strip coordinates and TCA levels; logical navigation and BOOT press
still need full-reader acceptance.

One contact retains its original screen/strip region. Region crossing,
multiple/reserved contacts and failed reads cancel the sequence until lift.
Canceled screen contacts retain a raw UI cleanup release but no tap/swipe;
canceled strip/failed expander reads clear held state without release-driven
navigation. Independent physical keys retain their own real release. Activity
suppression clears the capacitive mask too. No input heap allocation was added.

S31 SdmmcBlockDevice queries cache alignment and uses the existing single 4KiB
bounce allocation with DMA|INTERNAL|8BIT capabilities. The host gets that same
buffer, an eight-sector chunk limit and an internal-only alignment callback;
IDF's auto-allocation flag is cleared. Legacy S3 allocation/API paths are unchanged.
The Plus profile selects slot0 through native IDF defaults and CLK24/CMD25/D20-23
in 4-bit mode. USE_BLOCK_DEVICE_INTERFACE=1 is set even though USB MSC remains off.
Mounting validates sector0 before publishing the card, with no format-on-failure.

Host checks prove argument/ownership/error behavior; they do not prove actual
bus timings, card contents, mounted filesystem behavior or cache coherency.
The initial physical diagnostic must avoid filesystem mounting/NVS initialization,
read only known card sectors, and preserve/restore the complete application slot.
The full reader now guards Arduino's erase fallback and the SD backup is verified.
Fresh mutable Flash snapshots were taken before the retained reader deployment;
keep those original snapshots for its eventual explicit restoration. The runner
has previously restored/verified changed NVS and the full application on hardware.
The corrected input-startup image has now been flashed. Physical touch/open,
volume/cover keys, Home, repeated pages and ordinary reboot/resume/settings
retention passed. Sleep/wake and broader stress remain unaccepted; automatic
sleep is set to Never. SD rollback is deferred by the user.

## Physical diagnostic evidence, 2026-11-07

Parent `logs/20261107-222525-v14-peripheral-run.json` and
`logs/v14-peripheral-runtime-analysis.json` record native diagnostic 0.3.0.
The operator confirmed the target/key order and normal ThingsOS display, touch
and page turns after restoration. The production `MetalioPlusIo` transport was
linked into the diagnostic; the full reader and SdFat adapter were not run.

| Ordered target in portrait coordinates | Raw FT6336U report |
|---|---|
| 1: (60,60) | (80,69) |
| 2: (623,60) | (641,83) |
| 3: (342,608) | (326,608) |
| 4: (60,1155) | (58,1154) |
| 5: (623,1155) | (627,1175) |
| Cover left / center / right | (100,1600) / (200,1600) / (300,1600) |

All eight contacts had matching lift events and no touch-read errors. These
manual taps establish direction and region separation; the largest target
residual is about 29px and does not justify a precision or affine correction.
No mapping change was made.

TCA input bits were idle `0x83`; volume+, volume- and power changed them to
`0x82`, `0x81`, `0x03`, respectively, each returning to `0x83` on release.
BOOT61 stayed high and was not pressed.

SD slot0 initialized at 4-bit/20MHz with 31,129,600 sectors of 512 bytes.
Eight reads of sector0 returned the same CRC32 `190aa403`. No filesystem was
mounted and no SD write was performed; filesystem, EPUB and production block
adapter acceptance remain separate.

The host captured one USB disconnect and reconnected successfully. The earlier
false/false-before-open serial sequence caused repeatable USB-reset messages
on this Mac/device; the host now uses IDF Monitor's no-reset open ordering
(RTS/DTR asserted before open, RTS released before DTR). Host retry tests pass.
The original complete 0xae0000-byte ThingsOS app slot was restored and verified;
both Flash ranges outside the app slot retained their pre-test digests.

## Power wake investigation — 2026-11-08

The first native deep-sleep implementation arms GPIO2 EXT1 ANY_LOW for TCA P17.
The deferred diagnostic records `reset=8 wake=3`, proving deep-sleep EXT1 wake;
it does not establish full peripheral recovery. The baseline boots with SD
`stage=ready err=0 capacity=31129600`; after the controlled wake SD card
identification fails with `stage=card err=264` (`ESP_ERR_INVALID_RESPONSE`),
before any CSD capacity or filesystem mount. The user confirmed the SD error
screen. Physical sleep/wake acceptance remains **failed**.

A temporary bench image omits only P5/P14 rail cut during sleep to isolate the
rail dependency. It retains all other pin parking and wake configuration. This
is a diagnostic experiment, not a final low-power policy, and normal power
host tests that require rail cut do not describe this variant. The normal
source is preserved in the independent artifact directory. Trial state and
results are recorded under the original persistent recovery association;
restoration still occurs only on the user's explicit request.

The keep-rails experiment was physically accepted: the user could resume the
book, turn a page and return Home after the controlled cycle. Mac USB
enumeration required a cable replug, so the early wake-cause log was missed for
this image; do not infer a captured reset cause. This supports a rail dependency
without proving which physical rail feeds SD. The temporary source change was
removed.

`MetalioPlusBoot.cpp` now provides an early boot phase from `HalGPIO::begin()`
before `Storage.begin()`. It reuses the native I2C1 owner, reads TCA output
latches and configuration, sets only P14 then P5 to high outputs with the native
80ms settle per rail, and preserves all unrelated bits. Native display/key
initialization remains deferred to the established phase. There is no new
firmware heap allocation: the same eventual native bus initializes earlier.
Normal rail cut in `MetalioPlusPower.cpp` is restored. Sixteen real boot adapter
cases cover retained/cold latches, alternate probe address and transfer/hold/lock
failures; all pass alongside the original seventeen sleep cases. The compiled
ELF confirms early rail restoration precedes SD and the normal rail cut is
present. The user accepted three corrected-image cycles for reading position,
page turns and Home. One complete wake log confirms reset=8, wake=3 and SD ready;
the later cycles required USB replug and are physical observations without full
captured wake logs. A subsequent read-only check verified the current image,
immutable Flash and original recovery association, then rebooted the reader.
USB auto-enumeration remains unreliable; sleep current and battery operation
remain unmeasured. Automatic sleep remains Never. See parent
`logs/plus-early-rails-acceptance.json` for exact artifact and evidence links.

## Bounded frontlight increment — 2026-11-08

The user explicitly deferred intermittent USB replug investigation. Do not
request more USB sleep tests as a prerequisite for porting the remaining devices.

The pinned vendor `periph/frontlight.cc:15-21,227-259,288-299` and `.h:41`
establish GPIO6 cool/GPIO7 warm, active-high 20kHz/10-bit PWM and a 10% physical
output ceiling per channel. Those reference files were checked against their
pinned Git blobs. Plus now enables the existing SDK/HAL frontlight capability,
using the existing control panel and persisted settings. A trailing optional
`FrontlightConfig::maxOutputPermille` defaults to 1000 for existing profiles;
Plus uses 100. The total PWM budget is capped at 102/1023 before splitting
cool/warm, preserving the reader's constant-total mixing and active-low off
semantics. The startup kick obeys the same bound.

Real Arduino4 API recording tests exercise all percent/level/warmth settings,
off, boost kick, polarity and held-pad release, plus X4Pro PWM and Picco I2C
regressions. Review found and resolved separate-channel low-level rounding
with a failing constant-total test. The first bounded candidate was never
programmed; its trial plan is disabled. Only the corrected balanced candidate
was written and verified under the original recovery association.

Current balanced image: parent
`artifacts/crossmux-metalio-plus-s31-balanced-frontlight-20261108.bin`.
The image is 6,611,488 bytes, static RAM 107,928 and linked Flash 6,610,640.
There is no new explicit application heap allocation, bus, task or timer;
existing LEDC driver resources become active. Bounded idle/home readings show
164,135 internal bytes free, 97,568 historical minimum, 65,536 largest block,
and 14,935,908 PSRAM bytes free. These are not an endurance or memory-gain claim.
The full host suite is 1,529/1,535 with the same six prior failures.

The user accepted physical brightness, warmth and on/off controls ("亮度、色温和开关正常").
A subsequent explicit light sleep/wake cycle passed: light off asleep, page
turns after wake, and stored brightness/warmth retained.
Normal wake respects the existing Restore Light on Wake setting; with it off,
the expected state is dark until the user switches the light on. This does not
mean the stored brightness/warmth was lost. Battery gauge integration remains
separate; it must reuse native I2C1 and must not create a competing Wire bus or
alter charger/calibration settings as part of telemetry bring-up.


## Read-only BQ27220 increment — 2026-11-08

Plus enables gauge address 0x55 on native I2C1 SDA0/SCL1 at 100kHz. The actual
CX25601N charger is not configured as BQ25896: chargerAddr stays zero, and
zero designCapacityMah prevents unsealing or calibration writes. The Plus
BatteryMonitor routes word reads to MetalioPlusIo; legacy Wire helpers fail
closed. No bus recreation or explicit allocation is introduced. The existing
IDF native-device helper may allocate its first cached gauge device handle.

A native mutex protects a one-second cached snapshot: a six-byte incremental
read at 0x08 yields voltage/current and a two-byte read at 0x2C yields SOC.
The 66us bus-free interval and cache also apply on failures. Invalid SOC >100,
voltage outside the vendor sanity interval 2500..5000mV, failed transfers and
lock failures stay unknown; signed -1mA remains valid. Positive current reports
charging; it does not establish USB presence. Plus observes native USB SOF and
stays awake on USB POWERON, avoiding a new gauge-triggered pre-display sleep.
This is a cold-boot regression correction; intermittent replug work is deferred.

Production-driver host coverage includes 22 scenarios plus the existing full
battery harness. An executed real-helper/wake-switch regression caught the
USB cold-boot path and helper compilation guards; independent re-review found
no remaining issues. The full reader host suite passes 1531/1537, with the
same six prior failures (WeReadConnection, ReadingUiRegressions, UiFontFallback,
FileBrowserImages, WebServerStartupFailure, FirmwareUpdateRefresh). The earlier
matrix/static-analysis environment limitations remain; no full-CI claim.

The retained image is parent
`artifacts/crossmux-metalio-plus-s31-native-battery-20261108.bin`, SHA256
`d2f9629339eba3c332f55d0cb1df646cfddf2192845604dbeac67f27874e4095`.
Size 6,613,168, static RAM 107,968 (+40), linked Flash 6,612,316 bytes. Original
recovery association and immutable pre-write digests were verified; application
write verified. The user confirmed battery display 97%, opening books and page turns normal.
A captured PWR sample at 242995ms reports 97%, 4375mV, charging=1 with both
known flags true. This accepts the telemetry display/read path, not SOC accuracy.
The user subsequently accepted one explicit frontlight sleep/wake and
brightness/warmth retention cycle. See parent
`logs/plus-frontlight-sleep-acceptance.json`. First serial attachment may miss early boot diagnostics; no early
SD or gauge values are inferred from their absence. SOC accuracy, battery-only
endurance and current consumption remain unmeasured.

The serial collector was intentionally stopped by SIGINT after physical
acceptance. The report records KeyboardInterrupt and transaction_state=debugging
as expected for retained mode. No firmware restore or device reset was requested.


## Local Wi-Fi file transfer acceptance — 2026-11-08

No firmware or production-code change was needed. On the retained native-battery
image, native WiFi associated using WPA2 and obtained 192.168.0.106 on the Mac's
LAN. `/api/status` identified `metalio_eink4_plus` and the expected local version;
HTTP home, file manager, status/list endpoints and matching crosspoint.local
mDNS address worked. This is STA/LAN acceptance, not internet TLS or AP mode.

An original 2,286-byte test EPUB and 262,144-byte integrity TXT were uploaded
via HTTP into a newly created `Metalio-Network-Test-20261108-215320` directory.
Each was downloaded three times, all SHA256 matches. The actual Chinese browser
file manager then uploaded the TXT through its chooser/upload controls (observed
`[WS]` progress); the subsequent download also matched. Only the test integrity
file was removed; the original test EPUB remains for reproducibility. Uploads
were confined to the new directory. Ordinary credential/reading-cache changes
remain retained under the existing user instruction.

The user exited transfer, opened the received EPUB and confirmed Chinese text,
two page turns and Home. The serial log records web teardown and EpubReader
entry/exit with no captured panic. Bounded server samples had about 105KB free
internal RAM, 63,488 largest block and a historical minimum of 32,236 bytes;
after reading/Home the final sample had 143,487 internal and 13,887,212 PSRAM
bytes free. Mixed cache states prevent an endurance or leak conclusion.
The read-only serial collector was ended intentionally without restore/reset.

WiFi settings alone saves credentials then restarts to settings; the web server
is entered through File Transfer. WiFi list drawing also logged a few clipped
edge pixels; no crash or user-visible fault was reported, and a pixel-level UI
audit remains pending. AP/hotspot, Calibre, internet TLS and longer runs are
unaccepted. Parent `logs/plus-network-acceptance.json` and
`artifacts/network-acceptance-20261108-215320/` retain detailed evidence.
