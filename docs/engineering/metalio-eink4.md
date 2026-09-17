# Metalio E-Ink 4

Independent ESP32-S3 firmware target, model/board tag `metalio_eink4`, public
Nightly slug `metalio-eink4`. Hardware reference: `metalio-hw-test` 2.0.51,
`main/hal/metalio-e-ink-4/config.h`, `IOExpander.hpp`, and its SSD1677 driver.
The port was developed on SDK `5faf69e8` and rebased onto the current CrossMux
SDK dependency, including its SD-capacity fix; it does not migrate the reference's
ESP-IDF/LVGL application or add its audio, cellular or IMU features. Global touch
haptic feedback is supported as described below.

```sh
pio run -e metalio_eink4
CROSSPOINT_RC_HASH=$(git rev-parse --short=7 HEAD) pio run -e metalio_eink4_nightly
pio run -e metalio_eink4 -t upload --upload-port <verified-metalio-port>
pio device monitor --port <verified-metalio-port> --baud 115200
python3 -m unittest discover -s scripts/tests -p 'test_metalio_eink4.py'
```

The target uses the existing 16 MiB flash dual-OTA partition layout and the
prebuilt S3 TinyUSB core with octal PSRAM. Actual PSRAM capacity is detected at
boot. First installation must include CrossMux's bootloader/partition table;
do not place only the OTA application into the hardware-test firmware's layout.
The Nightly full-install manifest supplies the matching offsets and files.
Both legacy flavor manifests point to the same unified firmware. Tagged images
for other boards are rejected by the existing flash/OTA board-tag check.

## Hardware contract

| Function | Wiring / behavior |
|---|---|
| Display | GDEM0397T81, SSD1677, native 800×480; SCLK14 MOSI8 CS45 DC13 RST18 BUSY9, 10 MHz |
| SDMMC | 1-bit, CLK38 CMD40 D0=39; GPIO46 DAT3/CD input pull-up only |
| Shared I²C | SDA41 SCL42, 400 kHz, Arduino Wire transaction locking |
| TCA9555 | `0x20`, INT2 input pull-up; MAIN=P0.6, SCREEN=P0.5, touch reset=P1.1, shutdown pulse=P1.3 |
| Touch | CST816S `0x15`, active-low IRQ1, reset through the expander |
| Buttons | BOOT0=Confirm, POWER3=Power; P0.7=Down/next, P1.0=Up/previous, active-low |
| Virtual keys | Raw Home=(80,900) unchanged; Prev=(400,900) → Left, Next=(240,900) → Right |
| Battery | BQ27220 `0x55`: percentage and gauge-native charging status |
| Charger | Optional CX25601N `0x6B`: read status only; never interpreted as BQ25896 |
| RTC | PCF8563 `0x51`, existing system-UTC restore/writeback behavior |
| USB | Native USB19/20, Serial/JTAG and existing USB Drive/MSC workflow; TCA9555 P0.0 held high for flash/debug routing |
| Haptic | GPIO44, active-high motor; Off / Low 20 ms / Medium 35 ms / High 60 ms |

The expander preloads safe output levels before setting directions. Main power
is asserted before screen power; touch reset is held low for 10 ms and settles
for 120 ms after release. P0.4 amplifier, P0.1 amplifier routing and GPIO44
motor start low. P0.0 USB routing is preloaded high before enabling its output.
Unknown expander lines stay inputs.

## Display, input and shutdown

Metalio uses the shared SSD1677 driver with board-specific parameters. Explicit
FULL is `0xF7`; ordinary FAST is `0xFC`. Initial drawing, periodic HALF and
physical grayscale cleanup use two FAST phases: establish black from a white
previous plane, then paint the target from black. Each phase finishes before
RAM changes, and final BW/RED planes match the target. The BSP's HALF `0xD7` /
`0x6A` parameters remain in the board configuration but are not the default
HALF cleaning path. Border is `0x01` for FULL and `0x80` for partial/gray/park.
Power-off uses `0x83`, then deep sleep `0x10/0x03`.

The driver separately records physical grayscale residue and whether RED holds
a synchronized B/W baseline. The HAL's `DisplayRefreshContext` is passed with
each synchronous, asynchronous or grayscale-base request; `Normal` is the
backward-compatible default. `ContinuousReading` allows only a FAST request
with a synchronized baseline to reuse it. It cannot bypass initial drawing or
periodic cleaning. Reader helpers supply this context for text pages and XTC;
menus, image apps and sleep screens use the normal cleanup contract. There is
no persistent next-call permission. The SDK facade uses optional `WithContext` hooks whose default implementations
call the original driver methods. Only SSD1677 overrides these hooks; other
drivers require no signature or source changes.

One driver decision selects FAST, HALF, FULL or black-pulse cleaning for the
full-frame and deferred paths; window updates escalate to the same full-frame
clean when needed. Cleans complete synchronously; ordinary FAST retains async
overlap. BUSY timeouts keep the baseline unknown, prevent further RAM/sleep
commands while busy, and do not mark the controller powered off. `0xCC` leaves
analog power on, so shutdown still performs the required park sequence.

No LUT, voltage, SPI rate or discharge delay was changed. Constant fills use
the existing 128-byte stack chunk; grayscale retains strip rendering and its
board-configurable LUT. No additional full-screen buffer or polling task is
added. The framebuffer is 48,000 bytes; SDMMC retains its existing 4 KiB DMA
bounce buffer. Static sizes do not establish the runtime memory budget.

```sh
python3 -m unittest discover -s scripts/tests -p 'test_metalio_display.py'
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py
# Optional command tracing; black-pulse cleaning is already the board default.
PLATFORMIO_BUILD_FLAGS='-DSSD1677_PROBE_DEBUG=1' pio run -e metalio_eink4
```

The earlier A/B flag and FULL-after-gray trial policy have been removed.
Record image SHA-256, waveform BUSY time, visible transition, park and shutdown
pulse time separately. Ten gray/menu/sleep/wake cycles and 1/10-minute retention
checks remain part of physical acceptance; explicit FULL is the quality
comparison. The user confirmed the page-turn issue resolved and selected the
running black-pulse behavior for Nightly; this is not full ghosting acceptance.

The touch backend decodes the five-byte frame at register `0x02`; the ISR only
sets a pending flag. Held contacts are sampled on the existing 8 ms cadence,
idle contacts wait for IRQ, and I²C failures back off for two seconds.
Expander keys share one read per 20 ms. Failed reads cancel held input, never
synthesize a Home click, and recover when communication resumes and contact
returns idle. Missing touch leaves BOOT, Power and side navigation available.

Virtual keys are classified before coordinate mapping; other out-of-range
coordinates are discarded. Screen points map to `(rawY, 479-rawX)`, then pass
through the normal orientation transform. Crossing from screen to bezel keys
cancels that contact until release. Home short press returns Home; a 700 ms
hold uses the existing reader-menu action and consumes the subsequent short
release. Bezel Prev/Next now report Left/Right through the existing front-button
mapping; physical side buttons report Down/Up respectively and retain user
side-button swaps. Activity transitions retain input suppression. This key-map
correction follows hardware feedback. The user confirmed the corrected key mapping
on the physical device; this does not extend the earlier display-retention or
power-cycle acceptance.

The initial held Power gesture and its release are consumed on every boot.
Subsequent Power gestures and automatic sleep use existing settings. Shutdown
saves reading state, renders the sleep screen and parks the controller before
continuously sending 100 ms high/100 ms low power-key pulses until hardware
removes power, matching the reference board. MAIN/SCREEN rails stay enabled and
PA stays off. There is no three-pulse limit or ESP deep-sleep fallback, including
when USB maintains supply. Initialization retries every second; failed pulse
writes retry at the same cadence with error logs limited to once per second.
USB diagnostics remain available. Shutdown does not reconfigure the charger.

CX25601N external-power status is cached for one second. Absent/unreadable
chargers retry after two seconds and fall back to USB SOF activity and the
existing gauge charging indication. On legacy boards, a full battery connected
to a charge-only source may not be distinguishable from disconnected power.
No charge voltage/current, watchdog, private register, or dynamic-regulation
writes are performed; battery compatibility must be established separately
before introducing any charger control.

## Acceptance

Automated checks and hardware acceptance are separate. Use the checklist below
for complete acceptance; the dated session records only the observations made:

- Cold boot, reset, completely remove/reapply power; boot logs identify
  `metalio_eink4`, PSRAM, SD mount and RTC/gauge status without panic/OOM.
- FULL/HALF/FAST, window updates and four-gray reading; verify orientation,
  corners, AA and residual image after shutdown and an extended idle period.
- All physical/virtual keys, short/hold/release, rapid page turns, long Power
  held across boot, and contacts crossing activity/popup boundaries.
- Open an EPUB from SD, turn pages, save progress/settings and verify after
  reboot. Missing SD must use the existing recoverable SD-error screen.
- Set RTC, reset and remove power; verify UTC recovery. Test charging/unplugging
  with and without CX25601N and when the battery reaches full.
- USB MSC copy/rename/delete/large-file read; eject/cancel/disconnect and verify
  reboot, SD remount and opening the transferred book. Repeat three times.
- Existing Wi-Fi transfer/OTA and BLE page turner connect/disconnect/reconnect.
- Log internal free heap, historical minimum, largest free block and PSRAM
  before/after reading, BLE, Wi-Fi, USB transfer and sleep cycles. Confirm no
  downward trend; record cold boot/reset/repower independently.

Local build, local tests, hosted CI, public release, flashing and physical
acceptance must each be recorded independently.

### 2026-09-12 local flash session

- Target identified as ESP32-S3 revision 0.2, 16 MiB flash, embedded 8 MiB
  PSRAM; the previous firmware logged the reference CX25601N driver.
- Flashed `metalio_eink4`, version `1.5.8-metalio-eink4-rc+c2fb3467`, from
  the uncommitted implementation. Bootloader at `0`, partitions at `0x8000`,
  boot_app0 at `0xe000`, application at `0x10000`; all four esptool hash
  checks passed. No full-chip erase. The user already had a backup, so the
  additional backup attempt was stopped before flashing.
- Application SHA-256:
  `beb43a5b27003ca6c0bbba01598695cda80340949ce2cab28fa84ce6d4555b42`.
- First boot reached language selection. Subsequent interaction logs showed
  menus, settings, keyboard input, Wi-Fi connection and HTTPS image downloads
  (96,070-byte BMP and 198,363-byte JPEG), SD cache writes and grayscale
  rendering. Full refresh completed in about 3.3 s, ordinary refresh in
  386 ms, and the observed grayscale phase in 143–145 ms. These are controller
  completion logs, not visual confirmation of waveform quality or touch accuracy.
- A subsequent device restart remounted SD, restored settings, initialized
  the display and reconnected with saved Wi-Fi credentials. Its physical reset
  cause was not captured; the host reset command coincided with USB absence
  and did not execute. Do not count this as a controlled cold-boot/repower test.
- SNTP completed and the external RTC write/readback was verified. RTC
  recovery after removing power remains untested.
- Internal heap: language page free 204,456 B, largest block 155,636 B;
  Wi-Fi keyboard free 157,972 B, largest 114,676 B; image session minimum
  free 143,064 B, largest observed block 106,484 B. After restart, menu free
  204,760 B, largest 155,636 B. Runtime PSRAM usage was not logged.
- No captured panic, OOM or CST816S/TCA9555 communication failure. One Arduino
  `disconnect(): STA not started` message preceded a successful saved-network
  connection; this was not a connection failure.
- Still pending: user confirmation of display quality, four-corner touch and
  every physical/virtual key gesture, controlled reset/cold boot/complete
  repower, shutdown retention, EPUB progress, USB MSC, BLE, battery/USB changes
  and extended memory measurements. Hosted CI and publication were not run.

### 2026-09-12 refresh and reading regression session

- The first state-only test firmware incorrectly cleaned after every text-AA
  page. The user rejected this behavior. The reader baseline reuse
  fixes this without changing the saved refresh frequency.
- The reader fix was flashed first with the default cleaning policy (SHA-256
  `aa764e5a02b418c0e276a0185d1210bf7a05e12ec3f4bb5fb91f982b96a84f89`),
  then with the optional black-pulse policy and command tracing (SHA-256
  `588c50a9ab4ee41183191bb80c9139f723e5f2a2ffcbf36856744f2a78038ca9`).
  Both application writes passed esptool hash verification. The second image
  initially produced no captured serial output; after reconnecting, live
  reading logs confirmed that it was running. This is not a controlled boot test.
- The live black-pulse session captured consecutive text pages 1 through 14
  using one FAST `0xFC` activation plus the `0xCC` AA phase per page. Typical
  complete page rendering was 0.77–0.85 s; some pages took 1.32–1.44 s with
  longer CPU/render/write phases. Async activation's reported 0–3 ms is only
  submission time, not the panel's refresh duration.
- Clean transitions used two `0xFC` phases, each about 385–386 ms BUSY time;
  the complete B/W display call took about 1.09 s. Grayscale BUSY time was
  about 143–148 ms. No BUSY timeout or panic appeared in this capture. These
  measurements do not establish visual ghosting quality or total shutdown time.
- Internal free heap ranged from 82,488 to 168,896 B in the captured reading
  samples; historical minimum was 44,404 B and largest blocks ranged from
  31,732 to 73,716 B. These samples are not a long-duration leak test; runtime
  PSRAM usage remains unmeasured.
- Local checks passed: 47 Python tests, including real-driver command traces
  and the shared reader cadence harness, plus 24 SDK input checks. Physical
  visual acceptance and ten gray/menu/shutdown/startup cycles with 1/10-minute
  retention checks remain pending. At that stage the black-pulse policy remained opt-in; the subsequent
  approved refactor makes it the board default.
- Rebuilt `gh_release` (unified X3/X4), `waveshare_epaper_397`,
  `metalio_eink4` and `metalio_eink4_nightly`: all four passed. The rebuilt
  default Metalio image has SHA-256
  `fc800a9f3d342ccbb427345b61de3e51409814098ab83d58568b4796df512ecc`;
  it was archived locally, not flashed over the running black-pulse test image.
  No hosted CI, commit or public release was performed.

### Refactor and PR delivery

- The user confirmed the page-turn regression resolved and selected black-pulse
  cleaning as the board default for PR/Nightly delivery. This does not replace
  the pending long-retention and power-cycle checklist.
- Replaced the one-call permission with request-scoped reading context; kept
  physical residue pending even after a FAST reading page without a new gray
  overlay. Full/window/deferred paths share the clean decision, and BUSY
  failures cannot commit a false off state or continue writing RAM.
- Removed the temporary FULL-after-gray policy and opt-in black-pulse flag.
  Tests now compile the real SDK driver and include `ReaderRefresh.h` directly,
  with no production-source string extraction. Trace coverage includes both
  failed black-pulse phases, failed analog park, deferred completion, explicit
  FULL and leaving reading without another gray overlay.
- Rebased onto CrossMux main `fd4a74af` and its SDK dependency `8242f43`, which
  includes the existing SD-capacity repair (SDK PR #25). That dependency must
  land before the Metalio SDK PR; the firmware PR pins the pushed SDK revision.
- Local Python checks: 53 passed. SDK input checks: 24 passed, plus the touch
  activity host checks. Post-refactor firmware builds, package hashes, serial
  reflash and PR CI outcomes are recorded separately below as they complete.

### Driver compatibility scope

The original `PanelDriver::display`, `displayStart`, and `displayGrayscaleBase`
interfaces are unchanged. The three `WithContext` hooks are optional extensions;
legacy drivers retain their existing virtual dispatch, including specialized
async and grayscale-base behavior. The 22 files belonging to the other 11 panel
drivers match SDK dependency `8242f43` byte for byte. The Metalio policy remains
inside the configurable SSD1677 driver, with no duplicate platform driver.

A host compatibility test implements only the original interface and verifies
all three new hooks dispatch to it. The facade also forwards reading context
when an inverted grayscale-base request falls back to ordinary display.

### Key mapping validation (2026-09-12)

- Real `InputManager.cpp` host coverage verifies BOOT confirmation, separate
  expander directions, bezel Left/Right, failed-read release, and unchanged Home
  tap/hold events. Existing 24 SDK input checks also passed.
- Both Metalio build environments passed before rebasing onto the latest main.
  The tested application SHA-256 was
  `0a76680541efebd688a935ab926fad33173c1c2b4ac5fda19d3db23ac931a7a6`.
- The initial app0 write verified successfully but the device still selected
  app1. Reading the actual partition table and OTA metadata identified app1 at
  `0x650000`; rewriting that slot took 51.1 seconds and passed hash verification.
  Boot logs confirmed the expected build, SDMMC, RTC and saved settings.
- The user confirmed the corrected key mapping. Future serial updates must
  inspect the selected OTA slot; write verification alone does not establish
  that the new image booted. App-menu logs also reported a drawing-boundary
  warning; it is separate from this mapping change and was not addressed here.


## 2026-09-16 reference audit and haptic feedback

Reference: `/home/zzb/workspace/ink/Metalio-E-INK4`, particularly
`main/boards/metalio-e-ink-4/config.h`, `metalio_e_ink_4_board.cc`,
`metalio_touch.c` and `main/boards/common/IOExpander.hpp`.
These are software wiring evidence, not a new schematic or physical acceptance.
Workspace bases: CrossMux `d0889e7224c0`, SDK `e8e0276d0609`, plus local
uncommitted changes; the SDK gitlink is unchanged.

| Subsystem | Comparison and decision |
|---|---|
| SoC / storage | Retain independent S3 N16R8 target, existing dual-OTA layout, SDMMC CLK38/CMD40/D0=39 and input-only DAT3=46; do not copy the reference application partitions. |
| Display | Panel, 800×480 geometry, SPI pins and 10 MHz agree. Retain the physically selected CrossMux SSD1677 cleaning policy and grayscale lifecycle. |
| Touch / keys | Native coordinates, three bezel locations, GPIO1 IRQ, expander key pins and 10/120 ms boot reset agree. Add `0xA5=0x03` before deep sleep; failures log and still allow shutdown. Reset on boot restores touch after deep-sleep wake. No new light-sleep policy. |
| Power / expander | Keep safe latch preload and main/screen rail ordering; repeat 100 ms high/low shutdown pulses until power is cut, without an ESP deep-sleep fallback. Add P0.0 output HIGH for USB flash/debug routing, matching the reference FSUSB42UMX selection. |
| Haptic | Reference GPIO44 active-high, 35 ms timer pulse. Use board-calibratable 20/35/60 ms feedback for the entire touch surface and capability-gated settings. |
| Gauge / charger | BQ27220 at 0x55 and optional CX25601N at 0x6B agree. Preserve read-only charger status. Reference charge-current/voltage writes are intentionally not imported. |
| RTC | Retain PCF8563 at 0x51 and system-UTC restore/writeback. |
| Audio / microphone | Reference external BT audio module uses UART TX48/RX47 and I²S BCLK6/WS43/DOUT7/DIN17. CrossMux leaves audio/mic capabilities disabled and PA off; codec/module control needs a separate port. |
| IMU | Reference SC7A20H at 0x19, interrupt on TCA9555 P1.4. CrossMux keeps IMU disabled; no substitute QMI8658 driver or automatic rotation is enabled. |
| Cellular | Reference NT26 UART TX12/RX11, MRDY21/SRDY5. No CrossMux modem service is introduced. |
| USB camera | Reference switches P0.0 LOW for camera host use. CrossMux keeps HIGH for existing native-USB debug/MSC; no UVC host driver. |
| Wi-Fi / BLE | Retain existing CrossMux Wi-Fi and BLE page-turner paths, not the reference voice/network application. |

`FREEINK_CAP_HAPTIC` defaults to Metalio only and can be overridden with
`-DFREEINK_CAP_HAPTIC=0`. Unsupported boards remain at zero; enabling it on a
new board requires a motor pin/calibration implementation. The System setting
`hapticFeedbackLevel` appears immediately after the sound-feedback slot, even
when sound is unavailable. Values are Off=0, Low=1, Medium=2, High=3; missing or
invalid stored values use Medium. English/Chinese labels use the existing i18n
pipeline. Persistence and web exposure use the existing settings registry.

One accepted contact anywhere on the touch surface (screen, including blank
areas, or Home/Previous/Next) emits one press event before semantic gesture
classification. Swipes and long holds vibrate once at initial contact; motion,
release, physical buttons, Bluetooth, cancelled contacts and suppressed activity
transitions do not add pulses. An active pulse drops new requests rather than queuing/extending
vibration. Turning feedback off stops the motor before settings save/redraw;
the main loop also applies Off. Sleep stops it before panel parking and again
at the HAL input shutdown boundary, which holds GPIO44 LOW through deep-sleep
GPIO isolation. Board initialization releases that hold even in a capability-off
build. Light sleep is prevented from replacing the motor output configuration.

The SDK motor driver owns one `esp_timer`, allocated once at initialization and
reused until reset. The platform API has no static timer allocation alternative;
its timer task already exists, so no application task/stack is added. The local
Arduino 3.3.7 S3 `libesp_timer.a` disassembly shows a 32-byte timer payload
(`esp_timer_create`, calloc arguments 1×32), excluding allocator overhead; this
is version-specific, not measured heap use. Static state is one handle, one
critical-section lock and an int64 deadline. No per-pulse heap allocation or
framebuffer is added. The settings row uses the existing cold-path registry
allocation for four uint16 labels (8-byte payload plus allocator/row overhead);
incapable builds compile that row/field out.

GPIO changes and expiry checks share a critical section. A deadline check makes
an already dispatched old callback harmless after stop/restart. Allocation or
start failure leaves the motor off and logs the failure. Timing remains subject
to the shared ESP timer task's scheduling latency; physical pulse duration and
motor startup at Low require hardware validation.

### Hardware shutdown

Long-press shutdown and automatic sleep share the same Metalio HAL path. After
saving state, rendering the sleep image, stopping radios/haptics and parking the
display/touch/storage, SDK `shutdown()` continuously drives P1.3 high for 100 ms
then low for 100 ms until hardware removes power. MAIN/SCREEN remain enabled,
PA remains off, and USB diagnostics remain available. Initialization retries
every second; pulse write errors retry at the original cadence with rate-limited
logs. The SDK interface is now `[[noreturn]] void`; no GPIO-wake deep-sleep
fallback is compiled into the Metalio branch. Other devices are unchanged.

The user reported battery exhaustion after about two days following battery-only
long-press shutdown. The previous three-pulse fallback could leave peripheral
rails powered during MCU deep sleep; this is a suspected mechanism, not a
measured root cause. The reference also continuously pulses until power is cut.
A hardware fault or USB supply can keep this loop running indefinitely.

### Validation and physical evidence

- All 55 script checks passed, including the real InputManager, contact edges,
  screen/bezel hold/swipe/release and cancellation, motor timer failures/races,
  capability-disabled compilation, USB preload, touch-sleep command and real
  reader/SSD1677 cadence. Shutdown tests simulate more than three pulse cycles,
  permanent/transient I2C failure and initialization failure/recovery.
- Final ordinary Metalio build passed and was flashed to the user's ESP32-S3
  revision 0.2, MAC `10:20:ba:6e:08:70`, via `/dev/ttyACM1`. Read-back partition
  table matches the build; valid OTA sequence 2 selects app1 at `0x650000`.
  Wrote 6,060,080 bytes in 51.8 seconds; esptool hash verification passed. No
  settings/data partition or SD data was written.
- Tested ordinary application SHA-256:
  `73a430141b0a31da0d86c35934c01c51109aef074021ff3c141f56a5390f64f3`.
  Post-reset main-loop heap free/minimum/largest block was
  199,576 / 199,320 / 155,636 B, without observed panic/OOM in the short capture.
  This is not a sustained heap or battery-current measurement.
- Earlier shutdown capture showed automatic sleep after 600,000 ms, entry into
  the continuous pulse loop, a brownout message and serial disconnection. The
  user confirmed subsequent power-on was manual, not an observed spontaneous
  restart. This does not establish sustained rail-off or a low battery current.
- On 2026-09-17 the user reported testing complete and requested the related
  PRs. No quantitative current, rail-voltage, physical pulse-width or exhaustive
  sleep-cycle measurements were supplied; those remain unmeasured.
- The attempted desktop-entry FULL override was withdrawn after user feedback.
  It is absent from the final changes: original desktop refresh, reading cadence,
  antialiasing and SDK display waveforms are retained.
- Final Nightly and repository CI results are recorded with the PR handoff.

The SDK changes were integrated through
[freeink-sdk#29](https://github.com/0x1abin/freeink-sdk/pull/29). The application
pins merged commit `094976e1d47ad7120cf461fec5f6b737eaabf13f`; its source tree
`fa514adeac892085ec6205645c153b1f6dad4956` exactly matches tested feature commit
`4512f1441e1ab54dea8d9889cc35e021991413c8`. No SDK source or build-relevant file
changed during integration. [CrossMux PR #318](https://github.com/0x1abin/crossmux/pull/318)
contains the application/HAL changes. No application merge or firmware publication
is part of this work.
