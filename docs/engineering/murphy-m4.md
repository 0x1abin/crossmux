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
submodule. CrossMux adds only the build, frontlight settings, and experimental
product gates: AirPage and remote OTA are hidden, while reading, library,
settings, Web file transfer, and same-target SD firmware update remain.

The desktop target models the 800x480 panel, `murphy_m4` identity, touch and
rotation, RTC, buttons, dual-channel frontlight state, and Power-only wake. Use
the mouse for touch, arrows for Up/Down, `P` for Power, and `S` for sleep; M4
has no Home key, so `H` is ignored. It does not replace hardware tests for
display batches/ghosting, FT6336 polling, SDMMC contention, PWM curves, PSRAM,
or standby current.

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
  an e-paper refresh blocks the main loop; record the FT6336 task stack high-water mark.
- Verify GPIO0/1/2 buttons, concurrent 4-bit SDMMC/display use, ADC9 battery,
  active-low GPIO43 charging, and RX8010 power-loss retention/VLF handling.
- Verify warm/cool PWM curves and restoration after wake.
- Cycle deep sleep and confirm GPIO10/45 rails turn off, frontlight is off,
  GPIO0 wakes the device, and standby current is stable.
- Record internal heap, largest block and PSRAM through repeated reading,
  grayscale and Wi-Fi cycles; none may show a continuing decline.

AHT20, SC7A20, runtime panel-batch settings, AirPage, remote OTA and complex SD
fallback remain outside this experimental target.
