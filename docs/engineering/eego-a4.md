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
the board name comes from `BoardConfig::ACTIVE.name`, normal view content has a
symmetric 28 px safe margin, and the experimental build hides AirPage and
remote OTA. Reading, library, settings, Web file transfer, and same-target SD
firmware update remain available.

The desktop target models the 768x552 panel, `eego_a4` identity, symmetric
28 px content margin, touch/rotation, RTC, buttons, the screen Home/Back key,
and Power-only wake. Use the mouse for touch, arrows for Up/Down, `P` for
Power, `H` for Home/Back, and `S` for sleep. It does not replace hardware tests
for display waveforms/ghosting, GSL polling, bus timing, PSRAM, or standby
current.

## First flash and recovery

Before the first flash, back up the complete device flash:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 eego-a4-backup.bin
shasum -a 256 eego-a4-backup.bin
```

CI publishes an experimental artifact containing the app-only
`firmware.bin`, bootloader, partition table, a padded 16 MB
`eego-a4-recovery-16mb.bin`, SHA-256 sums, and this recovery guide. Restore a
backup with:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 eego-a4-backup.bin
```

The merged recovery image contains bootloader at `0x0`, partition table at
`0x8000`, `boot_app0.bin` at `0xe000`, and the app at `0x10000`. Do not use the
C3 release artifacts.

## Hardware release gate

- Verify the direction/edge pattern, Full/Fast/grayscale, forced Full after
  four Fast refreshes, bottom padding, and ghosting.
- Verify nine-point touch, all four gestures and orientations, plus screen-key
  Back tap and one-shot Home after 700 ms.
- Verify Up/Down/Power, RTC power-loss retention, concurrent SD/EPD access,
  battery ADC, and active-low GPIO11 charging indication.
- Cycle deep sleep/wake and verify GPIO3 held low, GPIO6 off, GPIO4 held, GPIO8
  high-level wake, and record standby current.
- Record internal heap, largest block, and PSRAM at boot, book open, grayscale,
  and Wi-Fi; repeated cycles must not show a continuing decline.
- Complete redistribution review for the GSL firmware table and display LUTs
  before any public release.

Frontlight, AirPage, remote OTA/catalog publication, GPIO44 hard power-off, and
additional hardware language environments remain outside this experimental target.
