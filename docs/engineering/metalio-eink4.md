# Metalio E-Ink 4

Independent ESP32-S3 firmware target, model/board tag `metalio_eink4`, public
Nightly slug `metalio-eink4`. Hardware reference: `metalio-hw-test` 2.0.51,
`main/hal/metalio-e-ink-4/config.h`, `IOExpander.hpp`, and its SSD1677 driver.
The port was developed on SDK `5faf69e8` and rebased onto the current CrossMux
SDK dependency, including its SD-capacity fix; it does not migrate the reference's
ESP-IDF/LVGL application or add its audio, cellular, haptic or IMU features.

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
| Buttons | BOOT0=Back, POWER3=Power; P0.7=Up/previous, P1.0=Down/next, active-low |
| Virtual keys | Raw Home=(80,900), Prev=(400,900), Next=(240,900) |
| Battery | BQ27220 `0x55`: percentage and gauge-native charging status |
| Charger | Optional CX25601N `0x6B`: read status only; never interpreted as BQ25896 |
| RTC | PCF8563 `0x51`, existing system-UTC restore/writeback behavior |
| USB | Native USB19/20, Serial/JTAG and existing USB Drive/MSC workflow |

The expander preloads safe output levels before setting directions. Main power
is asserted before screen power; touch reset is held low for 10 ms and settles
for 120 ms after release. P0.4 amplifier, P0.1 amplifier routing and GPIO44
motor remain low. Unknown expander lines stay inputs.

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
no persistent next-call permission. Other panel drivers ignore this context.

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
release. Prev/Next and side buttons use the existing logical mapping, including
user side-button swaps. Activity transitions retain input suppression.

The initial held Power gesture and its release are consumed on every boot.
Subsequent Power gestures and automatic sleep use existing settings. Shutdown
saves reading state, renders the sleep screen and parks the controller before
waiting 280 ms and attempting three 100 ms high/100 ms low pulses. If USB keeps
the MCU alive, restore the idle-high pulse line, log the outcome and enter the
normal GPIO3-wake deep-sleep fallback. Hardware rails stay powered until the
panel is parked; shutdown does not reconfigure the charger.

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
