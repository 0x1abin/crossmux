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

The default SSD1677 configuration targets the second production batch with
R13 fitted and uses the verified `0x50` pseudo-temperature. To build for the
first no-R13 batch, add `-DFREEINK_MURPHY_M4_BATCH1=1` in
`platformio.local.ini`; this selects `0x3C`. Keep this compile-time until both
batches pass the display gate.

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

- Verify direction, edge pattern, Full/Fast/grayscale and ghosting on both
  display batches.
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

AHT20, SC7A20, runtime panel-batch settings, remote OTA/catalog publication and
complex SD fallback remain outside this experimental target.
