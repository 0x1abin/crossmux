# Murphy M4 experimental target

`murphy_m4` is a separate ESP32-S3 N16R8 build. It inherits the normal DIO
flash mode, 16 MB partition table, one 48,000-byte framebuffer, OPI PSRAM and
USB CDC settings, and uses native 4-bit SDMMC storage.

```bash
pio run -e murphy_m4
pio run -e murphy_m4 -t upload
pio run -e simulator_murphy_m4 -t run_simulator
```

Hardware profiles and drivers live in the pinned `0x1abin/freeink-sdk`
submodule on its long-lived `crossmux` branch. The M4 button, FT6336U touch and
GPIO/LEDC frontlight paths track upstream FreeInk commit `e4d3cc33`; CrossMux
retains the two display batches, RX8010, GPIO43 charge input, SDMMC and product
gates. AirPage, reading, library, settings, Web file transfer, and same-target
SD firmware update remain available; remote OTA/catalog publication remains
withheld.

The M4 keeps its boot CPU frequency fixed because hardware validation found
FT6336U input unreliable after runtime clock changes. Idle power saving still
uses the normal 50 ms main-loop delay. Touch initialization reads back the
volatile mode, threshold, and report-rate registers before accepting input;
invalid status/event/coordinate frames are discarded without latching contact.

The desktop target models the 800x480 panel, `murphy_m4` identity, touch and
rotation, RTC, buttons, dual-channel frontlight state, and Power-only wake. Use
the mouse for touch, arrows for Up/Down, `P` for Power, and `S` for sleep; M4
has no Home key, so `H` is ignored. It does not replace hardware tests for
display batches/ghosting, FT6336U IRQ/reset behavior, SDMMC contention, PWM
curves, PSRAM, or standby current.

M4 batch detection runs before the normal GPIO input setup. R13 is a second
100 kΩ pull-up in parallel on GPIO1/KEY1; together with the key's 100 nF
capacitor it halves the charge time on batch 2. The firmware measures seven
50%-charge times in a fixed 28-byte stack array, caches only a result outside
the guard band in NVS key `cphw/m4_batch_v1`, and otherwise falls back to batch
2 without caching. A pressed Up key therefore cannot permanently select the
wrong batch. `HalGPIO::begin()` applies the result to touch before input starts;
`HalDisplay::begin()` reads the same HAL state before constructing the immutable
SSD1677 batch configuration. No batch state allocates from the heap.
[ESP32-S3 Datasheet v2.2](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
Table 2-8 maps GPIO1 to ADC1_CH0; its documented 60 µs power-up low glitch is
also shorter than the 50 ms settle delay before the charged reference sample.

Batch 1 (no R13) uses the `0x3C` pseudo-temperature and touch short-axis range
`[-52,553]`; batch 2 (R13 fitted) uses `0x50` and `[-47,514]`. The selected
temperature is applied to HALF and window refreshes. For recovery or hardware
diagnostics, `-DFREEINK_MURPHY_M4_BATCH1=1` forces batch 1 and bypasses the
probe. The product UI deliberately has no manual batch setting.

First-batch hardware validation measured 101 charge-time samples on GPIO1:
median 6008 µs, range 6004–6059 µs, and coefficient of variation 0.379%.
This passes the batch-1 interval and 10% stability gate. Second-batch hardware
validation remains required before removing the experimental release gate.

## First flash and backup

Back up the complete flash before the first write:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 murphy-m4-backup.bin
shasum -a 256 murphy-m4-backup.bin
```

Keep that backup outside the device. It is the only full-flash recovery image;
the Beta release contains only the four segments required by the Web installer.
Restore the original backup with:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0 murphy-m4-backup.bin
```

Do not flash an ESP32-C3 or eego A4 artifact. The first-install flow writes the
bootloader at `0x0`, partition table at `0x8000`, `boot_app0.bin` at `0xe000`,
and app at `0x10000` without overwriting NVS.

## Hardware release gate

- Verify direction, edge pattern, Full/Fast/Half/window/grayscale and ghosting
  on the second display batch; the first-batch RC probe stability gate is
  complete. Confirm first boot probes and caches the right batch, later boots
  use the cache, and holding Up during an uncached probe cannot persist a
  result.
- Verify four-corner touch, swipes and rotations, including a short touch while
  an e-paper refresh blocks the main loop; confirm GPIO44 active-low IRQ and
  that GPIO7 display reset is followed by successful FT6336U reinitialization
  with `0x00=0x00`, `0x80=0x16`, and `0x88=0x04` read back correctly. Confirm
  invalid frames neither create phantom touches nor leave an active touch stuck.
- Verify GPIO1/2 navigation and GPIO0 shared input: short press emits only
  Confirm (or only Power when the existing short-power option is enabled), and
  long press emits only Power.
- Exercise touch while repeatedly reading/writing RX8010; confirm both devices
  share I²C1 without conflicts or bus/device recreation. Also verify concurrent
  4-bit SDMMC/display use, ADC9 battery, active-low GPIO43 charging, and RX8010
  power-loss retention/VLF handling.
- Measure GPIO47/48 at about 25 kHz / 10-bit and verify the gamma curve at
  0/1/5/50/100%, both color-temperature endpoints, off, and wake restoration.
- Cycle deep sleep and confirm GPIO10/45 rails turn off, frontlight is off,
  GPIO0 wakes the device, and standby current is stable.
- Repeatedly alternate more than three seconds of idle time with touch input;
  confirm the CPU clock remains at its boot frequency and touch stays responsive.
- Record free heap, minimum free heap, largest block and PSRAM before/after
  initialization and through repeated touch/RTC/sleep, reading, grayscale and
  Wi-Fi cycles. The removed M4-only touch task must be absent, I²C handles must
  be allocated only at startup, and no metric may show a continuing decline.

AHT20, SC7A20, manual panel-batch settings, remote OTA/catalog publication and
complex SD fallback remain outside this experimental target.
