# Device Variants — Xteink X3 vs X4

> Deep reference for [CLAUDE.md](../../CLAUDE.md). How one firmware binary runs
> on both the Xteink X3 and X4, how the device is detected at boot, what differs
> between the two panels, and how to build / flash / verify for X3.

## TL;DR

**There is no X3 build. There is no X4 build. There is one build that is both.**

The same `firmware.bin` runs on X3 and X4. The firmware detects which panel it
is on at boot and adapts at runtime. To "build for X3", build any normal env and
flash it — the device identifies itself:

```bash
pio run -e gh_release        # international
pio run -e gh_release_cn     # Simplified-Chinese (see chinese-build.md)
pio run -t upload            # build + flash to whatever is plugged in
```

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

So the codebase uses **100% runtime dispatch** (`if (gpio.deviceIsX3())`,
~268 call sites), never `#ifdef`. The only compile-time SKU axis is *language*
(`ENABLE_CHINESE_VERSION`), which is orthogonal to hardware — see
[chinese-build.md](chinese-build.md).

## How detection works

`HalGPIO::begin()` calls `detectDeviceTypeWithFingerprint()`
([lib/hal/HalGPIO.cpp:150-189](../../lib/hal/HalGPIO.cpp), run at
[:197](../../lib/hal/HalGPIO.cpp)). Three-step resolution:

1. **Manual override** — NVS key `dev_ovr` (`0=auto, 1=X4, 2=X3`). If set,
   it wins immediately. See the caveat below.
2. **Cached result** — NVS key `dev_det`, written the first time detection is
   conclusive. Skips the probe on every later boot.
3. **Active I²C fingerprint** — only on first boot (empty cache).

Both NVS keys live in the `Preferences` namespace `cphw`
([HalGPIO.cpp:118-120](../../lib/hal/HalGPIO.cpp)).

The probe runs on the X3 I²C bus (`X3_I2C_SDA=20`, `X3_I2C_SCL=0`, 400 kHz;
[lib/hal/HalGPIO.h:20-23](../../lib/hal/HalGPIO.h)) and looks for three
**X3-only** chips ([HalGPIO.cpp:63-113](../../lib/hal/HalGPIO.cpp)):

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
  ([HalGPIO.cpp:174-188](../../lib/hal/HalGPIO.cpp)).

`DeviceType` enum and the `deviceIsX3()` / `deviceIsX4()` accessors are at
[lib/hal/HalGPIO.h:50-60](../../lib/hal/HalGPIO.h).

### The `dev_ovr` manual override — accurate caveat

`dev_ovr` is **read** at boot ([HalGPIO.cpp:153](../../lib/hal/HalGPIO.cpp)) but
**no firmware code writes it** — only the auto-detect cache `dev_det` is ever
written ([:178](../../lib/hal/HalGPIO.cpp), [:183](../../lib/hal/HalGPIO.cpp)).
There is **no settings screen, button combo, or web endpoint** to force a device
type. It is a support/recovery escape hatch you set **externally** (e.g. an
`nvs` partition edit / `esptool`), namespace `cphw`, key `dev_ovr`,
`uint8` value `2` for X3.

To clear a wrong cached detection, **erase NVS** (full chip erase, or wipe the
`nvs` partition) so the probe re-runs on next boot.

## What differs between X3 and X4

| Aspect | X4 | X3 |
|---|---|---|
| Panel | 800 × 480, **SSD1677** | 792 × 528, **UC81xx** |
| Framebuffer | 48000 B | 52272 B |
| Buffer allocation | `MAX_BUFFER_SIZE = 52272` (static, covers both) — [EInkDisplay.h:35](../../open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h) |
| Geometry switch | default 800×480 | `setDisplayX3()` before `begin()` — [HalDisplay.cpp:39](../../lib/hal/HalDisplay.cpp) |
| Display SPI clock | **20 MHz** (SSD1677 in-spec maximum) | UC81xx profile default, unchanged |
| Battery | ADC on GPIO0 | BQ27220 fuel gauge (I²C) — [HalPowerManager.cpp](../../lib/hal/HalPowerManager.cpp) |
| USB / charge detect | GPIO20 reads HIGH | sign of BQ27220 current — [HalGPIO.cpp:272-287](../../lib/hal/HalGPIO.cpp) |
| Clock persistence | ESP system clock; survives deep sleep/reset, not full power loss | ESP system clock plus DS3231 UTC backup — [HalClock.cpp](../../lib/hal/HalClock.cpp) |
| Tilt page-turn | none | QMI8658 gyro, X3-only — [HalTiltSensor.cpp:55](../../lib/hal/HalTiltSensor.cpp) |
| Theme button layout | stacked on the right | up-left / down-right — [BaseTheme.cpp:194](../../src/components/themes/BaseTheme.cpp), [LyraTheme.cpp:399](../../src/components/themes/lyra/LyraTheme.cpp) |
| Grayscale / refresh | SSD1677 fast LUT | UC81xx OEM pipeline + "AA-pre-BW" preconditioning — [EInkDisplay.h:56-94](../../open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h) |

The X4 FAST path is tuned in the application HAL rather than by modifying the
SDK gitlink: `crossPointX4Ssd1677Config()` selects the SSD1677 driver's
incremental DU sequence (`0x1C`), and `HalDisplay::begin()` selects a 20 MHz SPI
clock before the driver starts. The setting stays within the controller limit
and does not change the single-framebuffer RAM budget. Its tradeoff is panel
quality: the weaker DU waveform can leave more ghosting than the SDK's stock
`0xFC` sequence on some panel samples. Verify menus, text pages, high-contrast
borders, and grayscale images on real X4 hardware; if persistent residue is
unacceptable, retain 20 MHz but restore the stock waveform override. X3 never
constructs the SSD1677 driver, so neither tuning applies to it.

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

`clockUtcOffsetQ` remains a fixed display offset used by the status bar and
Standby faces; it does not change the process-wide timezone. Every device exposes
automatic sync, manual date/time, fixed offset, 12/24-hour format, and one-shot
sync under **Settings → System → Date & Time**. Chinese builds default a missing
`clockUtcOffsetQ` to UTC+8; an existing saved value is preserved.

The SPI display pins (`EPD_SCLK=8`, `EPD_MOSI=10`, `EPD_CS=21`, `EPD_DC=4`,
`EPD_RST=5`, `EPD_BUSY=6`) and the ADC button layout are **identical** on both
devices ([lib/hal/HalGPIO.h](../../lib/hal/HalGPIO.h)).

All rendering reads geometry from `getScreenWidth()` / `getScreenHeight()`
(never hardcoded 800/480), so layout follows the detected panel automatically —
this is why X3 "just works" without per-screen code (see
[ui-and-input.md](ui-and-input.md), golden rule #8).

## Build & flash for X3

Same envs as X4 (`platformio.ini`): `default`, `gh_release`, `gh_release_rc`,
`slim`, `gh_release_cn`, `gh_release_cn_rc`. Flash any of them to an X3:

```bash
pio run -e gh_release -t upload
# or, manual:
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 .pio/build/gh_release/firmware.bin
```

**Web flasher device target.** The in-firmware file/flash page
([src/network/html/FilesPage.html](../../src/network/html/FilesPage.html),
profile `X3: { width: 528, height: 792 }`) exposes an X3/X4 target selector.
That selector drives per-silicon image **patching**
([FirmwareFlasher.h](../../src/network/FirmwareFlasher.h) /
`patch_firmware_image.py`) so the stock bootloader accepts the OTA image — it
does **not** select a different firmware. Pick the target that matches the
physical device.

## Testing X3 without hardware — known limitation

You currently **cannot** exercise X3 geometry in the desktop / WASM simulator.
`simulator/shims/EInkDisplay.h` hardcodes 800×480 and makes `setDisplayX3()` a
no-op ([simulator/shims/EInkDisplay.h:12-18](../../simulator/shims/EInkDisplay.h)),
so the simulator always renders as X4. X3's 792×528 panel and its I²C
peripherals are not modeled. **Real X3 verification needs X3 hardware.** (Adding
X3 to the simulator is possible but out of scope here.)

## Verifying which device you're on

- **Serial** — boot log line `Hardware detect: X3`
  ([src/main.cpp:453](../../src/main.cpp)). The line above it prints the probe
  scores.
- **Web API** — `device` field is `"X3"` / `"X4"`
  ([CrossPointWebServer.cpp:382](../../src/network/CrossPointWebServer.cpp)).
- **UI** — the X3-only Tilt Page Turn item appears only when the QMI8658 is
  detected. Clock and Date & Time settings are available on every device.

## Adding a future device variant

The pattern generalizes. To add an "Xn":

1. extend `DeviceType` and add `deviceIsXn()` ([HalGPIO.h](../../lib/hal/HalGPIO.h)),
2. add an I²C (or other) fingerprint pass in `detectDeviceTypeWithFingerprint()`,
3. add `setDisplayXn()` + the panel's controller path in the SDK `EInkDisplay`,
4. keep optional peripherals (battery, RTC, IMU) behind their HAL classes and
   branch layout only where physical geometry requires it,
5. keep everything runtime-dispatched — do **not** introduce a build env.

See also: [build-system.md](build-system.md) (envs & flags),
[hardware-constraints.md](hardware-constraints.md) (RAM/flash budget),
[architecture-and-patterns.md](architecture-and-patterns.md) (HAL).
