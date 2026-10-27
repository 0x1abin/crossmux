# Device Variants — X3/X4 and Build-Only S3 Targets

> Sticky, X4 Pro, X4 Classic, PaperMono, [eego A4](eego-a4.md), [Murphy M4](murphy-m4.md),
> [Waveshare ePaper 3.97](waveshare-epaper-397.md), [Metalio E-Ink 4](metalio-eink4.md),
> [MindReset Read Pico](read-pico.md), and [LilyGo T5 S3](lilygo-t5s3.md) are separate ESP32-S3
> compile-time targets. The one-binary rule in this document applies only to
> the ESP32-C3 X3/X4 pair.

> Deep reference for [AGENTS.md](../../AGENTS.md). How one firmware binary runs
> on both the Xteink X3 and X4, how the device is detected at boot, what differs
> between the two panels, and how to build / flash / verify for X3.

## TL;DR

**There is no X3 build. There is no X4 build. There is one build that is both.**

The same `firmware.bin` runs on X3 and X4. The firmware detects which panel it
is on at boot and adapts at runtime. To "build for X3", build any normal env and
flash it — the device identifies itself:

```bash
pio run -e gh_release        # all UI languages and both content profiles
pio run -t upload            # build + flash to whatever is plugged in
```

ESP32-S3 devices use separate builds because their boards,
displays, input, storage, and power profiles differ from the combined ESP32-C3
image:

```bash
pio run -e x4pro
pio run -e x4c
pio run -e papermono
pio run -e sticky
pio run -e eego_a4
pio run -e murphy_m4
pio run -e waveshare_epaper_397
pio run -e metalio_eink4
pio run -e readpico
pio run -e lilygo_t5s3
pio run -e picco
```

Eight S3 targets are published by the shared Stable and Nightly matrices, including
MindReset Read Pico (`readpico-gh_release` and `readpico_nightly`). X4 Classic uses
its development environment `x4c` in Hardware CI and remains build-only.
LilyGo T5 S3 adds `lilygo_t5s3` for local development. Picco adds `picco` with
its stock partition table and app-only upload; it remains development-only. None of these targets joins the
public Nightly or OTA indexes. Each published target builds
one unified image, which is published under both legacy flavor pointers.
Manual flashing must use the matching build; the embedded
board tag rejects a tagged image for a different board. See
[firmware-release.md](firmware-release.md) for publishing and rollback details.

### BOOX Picco development target

`picco` is a separate ESP32-S3 development environment, outside CrossMux's
public Nightly/OTA registry. Its vendor bootloader, partition table, otadata and
recovery partition must be retained. The board's application-only upload hook
removes both the extra-image list and operands already captured in PlatformIO's
upload command. It permits the standard USB esptool application upload and
rejects full erase, filesystem upload and other upload protocols. Firmware updates use USB;
Wi-Fi OTA and SD-card `firmware.bin` updates are unavailable on this board.
Their settings entries and direct activity/backend entry points are disabled.
The shared SD writer also rejects a destination at the running application's
flash address, including the single-slot layout, before allocating or erasing.
Build/host checks do not establish physical display, power or recovery acceptance.

## Standby-display light sleep

### Adapter ownership

Matched Reader devices use the fixed upstream BSP through the CrossMux HAL.
Pin assignments, panel encoding/waveforms, touch backends and power sequencing
belong to the SDK; HAL owns initialization, storage serialization and sleep
handoffs. Reader refresh choices use existing grayscale capabilities. The
renderer centralizes the UC8279C whole-frame gray exception without exposing
another SDK interface or duplicating A4 conditions in individual activities.

Device macros remain for board selection, firmware tags and code/assets that
other devices must not link. Optional CrossMux display extensions and the
M4/Waveshare/ReadPico/LilyGo integrations remain separate from the matched BSP.
The A4 Home/layout changes and cache rebuilding are recorded in
[eego-a4.md](eego-a4.md#reader-adapter-convergence). Historical hardware results
do not establish acceptance for the current adapter.

Standby-display light sleep is enabled for the runtime-selected X3/X3 UC8279 and
X4 profiles. The HAL arms GPIO3 plus a timer and consumes the released power
button before returning to the Activity. ESP32-C3 isolates ordinary GPIOs in
light sleep, so the HAL exempts GPIO13 from that isolation: it keeps the X3 SD
rail powered or the X4 battery latch asserted. Display state and all other power
rails remain unchanged.

Repeated timer wake, power-button wake, and button, display, and SD recovery
after wake passed hardware validation on both X3 and X4. X4 sleep current has
not yet been quantified, so no measured power reduction is claimed. Read Pico
has its own PMU/expander light-sleep path; other S3 targets remain in the normal
downclocked standby loop. A successful build alone is not a hardware acceptance
test.

Bluetooth Page Turner Beta is enabled in the C3 hardware flavors and the S3
profiles using `s3_ble_psram`, including development, Nightly and Stable.
The runtime setting remains disabled by
default; existing saved choices are kept. EEGO A4 and native simulator builds
use the SDK BLE stub.

X3/X4 profiles share `c3_hardware`: internal RAM, a Flash controller and paged
coverage indexes for large SD fonts. The HAL protects the entire C3 BLE host
lifetime from manual 10 MHz idle downclocking. Sticky, X4 Pro, X4 Classic, Paper
Mono, Murphy M4, Waveshare 3.97, Metalio E-Ink 4, Read Pico and LilyGo T5 S3
inherit `s3_ble_psram` through their hardware profiles, including published
flavors where present. Sticky and Read Pico keep their tuned controller-only
cores. EEGO A4 uses the prebuilt TinyUSB graph with BLE stubs; other USB-capable
boards also keep the prebuilt graph. C3 never inherits the S3 allocator or IPC
wrapper.

X4 user tests confirm reading/page-turner operation; quantitative endurance and
X3/other-board hardware acceptance remain separate. See
[C3 Bluetooth](c3-bluetooth.md) for configuration, ownership and validation.

The diagnostic PSRAM mode additionally requires 256 KiB free and a 32 KiB
contiguous PSRAM block. Before starting the Host it allocates and immediately
frees a 16-byte probe through the linked NimBLE allocator, rejecting allocation
failure or a non-PSRAM address. It never silently falls back to internal RAM.
`BLE` logs report internal/PSRAM free, historical minimum and largest block at
start, stop, link transitions and every ten seconds while running, plus task
stack high-water marks. Historical minima are since boot, not per-phase peaks.
These are diagnostic safeguards, not a measured production memory budget.

All BLE start callers hold the render lock and use `BleInput` for the memory
gate, at most one recovery, and immediate recheck/start. Bluetooth settings and
key mapping use the same Explicit gate (70 KiB free / 24 KiB largest internal
block); only a rejected gate unloads the SD reading font and clears font caches.
An already-running host neither recovers memory nor initializes again. BLE
synthetic press and release frames both have zero hold duration, independent of
previous physical-key or touch durations.

Reader BLE startup keeps its 80 KiB free / 32 KiB largest internal-block gate.
On a low-memory start attempt, it releases rebuildable font caches under the
render lock and immediately retries without unloading the reading font. If the
gate still fails, it retries after two seconds. Chapter building, Wi-Fi, USB
Drive and sleep still stop BLE; eligible readers restart the host afterwards
and use the SDK's bonded-device reconnection. Hardware reconnection and memory
headroom must be verified on-device, not inferred from a successful build.

The X3-vs-X4 choice is **not** a compile-time decision. Do not add a `-DX3`
build flag or a `[env:...x3]` — see the next section for why.

## Why runtime detection, not a build env

X3 and X4 are the **same SoC** (ESP32-C3, 16 MB flash) with the **same
[partitions.csv](../../partitions.csv)** (dual 6.25 MB OTA app slots). They
differ only in the e-ink panel and a few I²C peripherals — all resolved at
runtime. A compile-time split would:

- double every release artifact (`gh_release` × {x3, x4}) for zero code benefit,
- break the "flash any official release to any device" UX (web flasher, OTA),
- add `#ifdef` branches where today there is one tested code path.

The shared C3 image uses runtime dispatch (`if (gpio.deviceIsX3())`) for the
X3/X4 choice. Language and content region are runtime
settings; there is no compile-time language SKU — see [chinese-build.md](chinese-build.md).

## How detection works

[`HalGPIO::begin()`](../../lib/hal/HalGPIO.cpp) calls
`detectDeviceTypeWithFingerprint()`. Three-step resolution:

1. **Manual override** — NVS key `dev_ovr` (`0=auto, 1=X4, 2=X3`). If set,
   it wins immediately. See the caveat below.
2. **Cached result** — NVS key `dev_det`, written the first time detection is
   conclusive. Skips the probe on every later boot.
3. **Active I²C fingerprint** — only on first boot (empty cache).

Both NVS keys live in the `Preferences` namespace `cphw`
([HalGPIO.cpp](../../lib/hal/HalGPIO.cpp)).

The probe runs on the X3 I²C bus (`X3_I2C_SDA=20`, `X3_I2C_SCL=0`, 400 kHz;
[lib/hal/HalGPIO.h](../../lib/hal/HalGPIO.h)) and looks for three
**X3-only** chips through the SDK's canonical fingerprint
([XteinkDetect.cpp](../../freeink-sdk/libs/hardware/XteinkDetect/src/XteinkDetect.cpp)):

| Chip | Role | I²C addr | Signature check |
|---|---|---|---|
| TI **BQ27220** | fuel gauge | `0x55` | SOC ≤ 100 % **and** voltage 2500–5000 mV |
| **DS3231** | RTC | `0x68` | seconds register is valid BCD |
| QST **QMI8658** | IMU | `0x6B` / `0x6A` | `WHO_AM_I == 0x05` |

Each probe pass scores 0–3. The pass runs **twice**:

- **score ≥ 2 on both passes ⇒ X3** (cached).
- **score == 0 on both passes ⇒ X4** (cached).
- **anything in between ⇒ X4 fallback, _not_ cached** — so an inconclusive
  first boot re-probes next time instead of locking in a guess
  ([HalGPIO.cpp](../../lib/hal/HalGPIO.cpp)).

`DeviceType` enum and the `deviceIsX3()` / `deviceIsX4()` accessors are at
[lib/hal/HalGPIO.h](../../lib/hal/HalGPIO.h).

### The `dev_ovr` manual override — accurate caveat

`dev_ovr` is **read** at boot ([HalGPIO.cpp](../../lib/hal/HalGPIO.cpp)) but
**no firmware code writes it** — only the auto-detect cache `dev_det` is ever
written on confirmed SDK verdicts.
There is **no settings screen, button combo, or web endpoint** to force a device
type. It is a support/recovery escape hatch you set **externally** (e.g. an
`nvs` partition edit / `esptool`), namespace `cphw`, key `dev_ovr`,
`uint8` value `2` for X3.

To clear a wrong cached detection, **erase NVS** (full chip erase, or wipe the
`nvs` partition) so the probe re-runs on next boot.

## What differs between X3 and X4

| Aspect | X4 | X3 |
|---|---|---|
| Panel | 800 × 480, **SSD1677**, **UC8179**, or **UC8279** by production batch | 792 × 528, **UC8253** or **UC8279d** by production batch |
| Framebuffer | 48000 B | 52272 B |
| Buffer allocation | Internal heap, allocated once at the runtime panel size | Internal heap, allocated once at the runtime panel size — [FreeInkDisplay.cpp](../../freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp) |
| Geometry switch | default 800×480 | `setDisplayX3()` before `begin()` — [HalDisplay.cpp](../../lib/hal/HalDisplay.cpp) |
| Display SPI clock | **10 MHz** by default | **10 MHz** by default — [BoardConfig.h](../../freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h) |
| Battery | ADC on GPIO0 | BQ27220 fuel gauge (I²C) — [HalPowerManager.cpp](../../lib/hal/HalPowerManager.cpp) |
| USB / charge detect | GPIO20 reads HIGH | No `usbDetect` GPIO; SDK `BatteryMonitor::isCharging()` infers charging from BQ27220 current — [HalGPIO.cpp](../../lib/hal/HalGPIO.cpp) |
| Clock persistence | ESP system clock; survives deep sleep/reset, not full power loss | ESP system clock plus DS3231 UTC backup — [HalClock.cpp](../../lib/hal/HalClock.cpp) |
| Tilt page-turn | none | QMI8658 gyro, X3-only — [HalTiltSensor.cpp](../../lib/hal/HalTiltSensor.cpp) |
| Theme button layout | stacked on the right | up-left / down-right — [BaseTheme.cpp](../../src/components/themes/BaseTheme.cpp), [LyraTheme.cpp](../../src/components/themes/lyra/LyraTheme.cpp) |
| INX front-button hints | four bottom segments aligned to the X4 keys | four bottom segments using the wider X3 spacing; labels remain orientation-aware — [InxTheme.cpp](../../src/components/themes/inx/InxTheme.cpp) |
| Grayscale / refresh | SDK pipeline for the detected controller | SDK UC8253/UC8279d pipeline + "AA-pre-BW" preconditioning — [FreeInkDisplay](../../freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h) |

After resolving the device, `HalGPIO::begin()` asks the SDK to probe the live
display bus before SPI initialization. X3 uses the OEM VER-byte protocol; X4
uses the SDK's display-controller fingerprint. OEM NVS `hw_calib/screenType`
is diagnostic only for these C3 devices. The selected controller, geometry,
10 MHz default SPI clock and refresh waveforms belong to the SDK. The HAL
preserves the detected X3 UC8279 sibling through `setDisplayX3()` and keeps its
existing wake/resync policy; it does not replace the controller's waveforms.

`HalGPIO::isUsbConnected()` uses the board's digital `usbDetect` pin when one
exists, otherwise asks SDK `BatteryMonitor` for external-power presence and
falls back to charging only when that state is unknown. Both X3 profiles mark
`usbDetect` unassigned because GPIO20 is I²C SDA. BQ27220 positive battery
current indicates charging; charge termination at a full battery can therefore
report disconnected while USB remains plugged in. This is a charging inference,
not a separate USB-presence measurement.

### Unified system clock and optional RTC

The POSIX/ESP system UTC clock is the runtime source on every device.
`HalClock::begin()` probes the optional external RTC and uses it only to restore
an invalid system clock after power loss. A stopped, unreadable, absent, or
invalid RTC naturally falls back to the same software-clock path; business and
UI code never branch on X3/X4 or RTC availability.

Network and manual updates set the system clock first, then write the UTC value
back to an available RTC. A failed RTC write is logged but does not invalidate
the successfully updated system time. The RTC is not polled periodically while
the device is running.

Every device exposes one **Settings → System → Date & Time** page, including
devices without an external RTC. It contains automatic sync, current date/time,
timezone, daylight saving time, 12/24-hour format, header clock visibility, and
one-shot sync. Manual date/time editing is enabled only when automatic sync is
off; one-shot sync remains available in either mode.

The upstream city/fixed-offset timezone table and Auto/On/Off DST policy apply
the process-wide timezone. Headers, standby faces, manual date/time conversion,
and reading analytics use that same timezone. Manual edits reject invalid dates
and nonexistent local times during a DST jump; repeated fall-back times use the
system library's automatic choice. Existing historical analytics are not rewritten.

`clockUtcOffsetQ` is retained for settings compatibility and the upstream
fixed-offset migration when `clockTimezone` is unset. First-start Simplified
Chinese defaults to UTC+8 and other languages to UTC+0; upgrades retain saved
settings. Header clocks and the date/time page show valid system time after
either manual editing or NTP sync, independently of external RTC availability
and the legacy `clockHasBeenSynced` flag.

The SPI display pins (`EPD_SCLK=8`, `EPD_MOSI=10`, `EPD_CS=21`, `EPD_DC=4`,
`EPD_RST=5`, `EPD_BUSY=6`) and the ADC button layout are **identical** on both
devices ([lib/hal/HalGPIO.h](../../lib/hal/HalGPIO.h)).

All rendering reads geometry from `getScreenWidth()` / `getScreenHeight()`
(never hardcoded 800/480), so layout follows the detected panel automatically —
this is why X3 "just works" without per-screen code (see
[ui-and-input.md](ui-and-input.md), golden rule #8).

## Build & flash for X3

Same envs as X4 (`platformio.ini`): `default`, `gh_release`, and `gh_release_rc`.
Flash any of them to an X3:

```bash
pio run -e gh_release -t upload
# or, manual:
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 .pio/build/gh_release/firmware.bin
```

**Web flasher device target.** The in-firmware file/flash page
([English Files page](../../src/network/html/en/FilesPage.html) and
[Chinese Files page](../../src/network/html/zh-CN/FilesPage.html),
profile `X3: { width: 528, height: 792 }`) exposes an X3/X4 target selector.
That selector drives per-silicon image **patching**
([FirmwareFlasher.h](../../src/network/FirmwareFlasher.h) /
`patch_firmware_image.py`) so the stock bootloader accepts the OTA image — it
does **not** select a different firmware. Pick the target that matches the
physical device.

## Testing X3 without hardware

The pinned desktop simulator fork provides an X3 environment with the 792×528
framebuffer, X3 board profile, and simulated tilt input:

```bash
pio run -e simulator_x3 -t run_simulator
```

Use real X3 hardware for final electrical, sensor, power, and display-waveform
verification.

## Verifying which device you're on

- **Serial** — boot logs the active board name, such as
  `Hardware detect: xteink_x3`, `xteink_x3_uc8279` or `xteink_x4`
  ([src/main.cpp](../../src/main.cpp)). First-boot
  probing logs its scores and verdict in [HalGPIO.cpp](../../lib/hal/HalGPIO.cpp);
  cached and manual selections log their source instead.
- **Web API** — the shared C3 image retains `"X3"` / `"X4"` in the `device`
  field. Other boards use `BoardConfig::ACTIVE.name`
  ([CrossPointWebServer.cpp](../../src/network/CrossPointWebServer.cpp)).
- **UI** — the X3-only Tilt Page Turn item appears only when the QMI8658 is
  detected. Clock and Date & Time settings are available on every device.

### X3/X4 adapter acceptance

After changing the SDK pin or HAL handoff, verify both devices and available
controller batches: cold boot, cached detection, display/grayscale refresh,
button input, SD access, deep sleep and standby timer/button wake. Check X3
USB insertion/removal during charging and after charge termination; confirm
that GPIO20 remains on the I²C bus and no digital USB read uses it. Host tests
and a shared C3 build verify software integration; they do not replace this
physical acceptance, and earlier light-sleep results do not validate a new
adapter revision.

On 2026-10-26, the UC8253 X3 candidate with firmware SHA-256
`fa5118d4f1a969f15e07f417a12d2eec2568f9d2e6e350dc7c1397282dbdaa40`
passed flash verification, fresh detection (both probe scores 3/3), two cached
restarts, IMU/RTC/SD initialization and a three-minute idle serial check. The
previous firmware's uninitialized-Wire errors did not recur. The tester confirmed
physical buttons, screen, reading/page turns, sleep/power-button wake and unplugged
standby/wake. This evidence applies to that candidate; X4, UC8279d X3 and
quantitative power/current measurements remain unverified.

## Adding a future device variant

Use the [port-device-bsp skill](../../.agents/skills/port-device-bsp/SKILL.md)
to establish the hardware contract, select an integration path, and record build
and physical acceptance evidence.

X3/X4 retain their shared image. A future runtime variant may share an image only
when silicon, partitions, initialization and reliable device identification are
compatible. Extend detection and panel selection only for that verified case;
do not add every new board to the X3/X4 `DeviceType` enum.

Otherwise add a separate hardware profile and build environment, following the
existing S3 targets in [platformio.ini](../../platformio.ini). Reuse the SDK's
board profiles and controller drivers where supported; keep application access
to peripherals behind the HAL and adapt layout through renderer geometry.
Unsupported chip architectures require a platform/toolchain assessment before
BSP implementation. A successful build is not physical acceptance, and a new
build target does not automatically enter public Nightly or OTA indexes.

See also: [build-system.md](build-system.md) (envs & flags),
[hardware-constraints.md](hardware-constraints.md) (RAM/flash budget),
[architecture-and-patterns.md](architecture-and-patterns.md) (HAL).
