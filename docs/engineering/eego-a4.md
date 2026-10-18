# eego A4 experimental target

`eego_a4` is a separate ESP32-S3 N16R8 build. X3/X4 remain one ESP32-C3
runtime-selected binary; an image for one MCU family must never be flashed to
the other. `FirmwareFlasher` checks the ESP image chip ID before erasing or
writing the target partition.

```bash
pio run -e eego_a4
pio run -e eego_a4 -t upload
pio run -e simulator_eego_a4 -t run_simulator
```

The target inherits the normal DIO flash mode, 16 MB partition table, single
framebuffer, and USB CDC settings. Hardware descriptions and drivers live in
the pinned `0x1abin/freeink-sdk` submodule; CrossMux only adds product behavior:
the board name comes from `BoardConfig::ACTIVE.name`, normal view content uses the upstream
viewable margins `{top=24, right=24, bottom=8, left=24}`, and AirPage is available. Reading, library,
settings, Web file transfer, USB Drive and same-target SD firmware update remain
available. Existing Nightly/OTA registration is preserved; this local rehearsal
publishes no firmware.

The desktop target models the 768x552 panel, `eego_a4` identity, upstream
24/24/8/24 viewable margins, touch/rotation, RTC, buttons, the screen Home/Back key,
and Power-only wake. Use the mouse for touch, arrows for Up/Down, `P` for
Power, `H` for Home/Back, and `S` for sleep. It does not replace hardware tests
for display waveforms/ghosting, GSL polling, bus timing, PSRAM, or standby
current.

## First flash and backup

Before the first flash, back up the complete device flash:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 eego-a4-backup.bin
shasum -a 256 eego-a4-backup.bin
```

Keep that backup outside the device. It is the only full-flash recovery image;
the Beta release contains only the four segments required by the Web installer.
Restore the original backup with:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 eego-a4-backup.bin
```

The first-install flow writes the bootloader at `0x0`, partition table at
`0x8000`, `boot_app0.bin` at `0xe000`, and the app at `0x10000` without
overwriting NVS. Do not use the C3 release artifacts.

## Hardware release gate

- Verify the direction/edge pattern, Full/Fast/grayscale, bottom padding and
  ghosting. Refresh cadence belongs to the caller; the SDK does not force Full
  after four Fast requests.
- Verify nine-point touch, all four gestures and orientations, plus configured
  Home tap/double-tap/hold actions without duplicate Back events.
- Verify Up/Down/Power, RTC power-loss retention, concurrent SD/EPD access,
  battery ADC, and active-high GPIO11 charging indication.
- On revisions fitted with the optional LM3630A, verify brightness control and
  frontlight restoration after wake; revisions without it must hide the setting.
- Cycle deep sleep/wake and verify GPIO3 held low, GPIO6 off, GPIO4 held, GPIO8
  high-level wake, and record standby current.
- Record internal heap, largest block, and PSRAM at boot, book open, grayscale,
  and Wi-Fi; repeated cycles must not show a continuing decline.
GPIO44 hard power-off and additional language-specific hardware environments
remain outside this target.

## Fixed upstream adapter (2026-10-13)

The reviewed SDK adopts upstream `EEGO_A4` board, UC8279C/GSLX680 input/display,
HSPI SD, PCF8563 RTC and power/frontlight adaptation. The 52,992-byte B/W panel
framebuffer follows SDK allocation; no extra UI framebuffer is added. The
LM3630A at `0x36` is probed before deciding whether frontlight controls are
available. GPIO11 charging is active-high; Power GPIO8 is active-high with a
pull-down. Deep-sleep preparation writes the GSL halt command (`0x88` to
register `0xE0`), releases its I²C pins and holds touch reset low. The HAL logs
a failed halt write; GPIO8 remains the power-button wake source.

The target retains the prebuilt `dio_opi` Arduino/TinyUSB graph for USB Drive.
The SDK BLE stub is accepted for this model. The vector-font profile retains
its 24 KiB loop stack and 16 KiB render stack. Non-vector builds use FreeInkUI's
weak 16 KiB loop default; the linked getter must be checked after rebuilding.
measure its high-water mark during WeRead login/download, EPUB indexing, network
transfer, settings and sleep transitions. This is a stack budget, not measured
headroom. Check USB Drive entry stops filesystem users and exit restarts.

Simulator/build tests cannot verify touch recovery after wake, optical output,
frontlight variations or sleep current. Keep first-flash offsets tied to the
actual generated partition/image metadata and preserve the backup above.

## Reader adapter convergence

The Reader uses upstream overlay-mask encoding and capability-based grayscale
refreshes; UC8279C owns conversion to native panel planes. Whole-frame gray
passes include text, highlights and the status bar. Home actions follow shared
settings, sliders use Back to cancel, and views use the board's 24/24/8/24
insets without the old status-bar 28 px clamp or extra 4 px offset, including INX.
The larger viewport invalidates old section caches by existing layout checks;
books, source-range clippings and cache formats are preserved.

Storage shutdown runs on every board. The HAL closes the frontlight before
touch releases shared I2C and retains configured latches, including GPIO4,
once before SDK power-down. Wake uses the common early debounce verification.
Hardware acceptance must repeat corner visibility, gray transitions and wake
with this implementation; prior optical calibration is historical evidence.
