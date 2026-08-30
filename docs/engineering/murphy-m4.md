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
submodule on its long-lived `crossmux` branch. GPIO0 is the independent power
key, GPIO1/2 are Up/Down, and the FT6336U uses fixed-cadence background polling;
CrossMux retains the two display batches, RX8010, GPIO43 charge input, SDMMC and
product gates. AirPage, reading, library, settings, Web file transfer, and
same-target SD firmware update remain available; remote OTA/catalog publication
remains withheld.

The M4 keeps its boot CPU frequency fixed because hardware validation found
FT6336U input unreliable after runtime clock changes. Touch initialization reads
back the volatile mode, threshold, and report-rate registers before accepting
input. GPIO46 TOUCH_INT is unusable on this board, so a core-0 task samples every
10 ms and latches the first complete gesture while the main loop is blocked by
an e-paper refresh. The task has a static 3072-byte stack and TCB, creates no
queue or heap allocation, and leaves all gesture classification in the normal
HAL input path. Invalid frames are discarded; repeated failed reads release a
stale contact after 100 ms.

The desktop target models the 800x480 panel, `murphy_m4` identity, touch and
rotation, RTC, buttons, dual-channel frontlight state, and Power-only wake. Use
the mouse for touch, arrows for Up/Down, `P` for Power, and `S` for sleep; M4
has no Home key, so `H` is ignored. It does not replace hardware tests for
display batches/ghosting, FT6336U IRQ/reset behavior, SDMMC contention, PWM
curves, PSRAM, or standby current.

M4 does not probe its hardware batch. Missing, unreadable, or invalid preference
data selects the market-default batch 2. The Hardware Batch row appears directly
above Check for updates in System settings. Opening it shows a Batch 1/Batch 2
picker with the active batch selected; cancelling or selecting that same value
does not write or restart. A changed value is written to `cphw/m4_batch_v3` and
takes effect through an immediate restart. A failed NVS write leaves the active
batch unchanged and does not restart. The former automatic-detection key
`cphw/m4_batch_v2` is deliberately ignored, so an upgrade without an explicit
manual choice also starts on batch 2.

`HalGPIO::begin()` reads the preference and applies it to touch before input
starts. `HalDisplay::begin()` reads the same HAL state before constructing the
immutable SSD1677 batch configuration. Batch 1 (no R13) uses the `0x3C`
pseudo-temperature and touch short-axis range `[-52,553]`; batch 2 (R13 fitted)
uses `0x50` and `[-47,514]`. The selected temperature is applied to HALF and
window refreshes.

The first-batch input build was also sampled for 70 seconds after startup. The
touch task retained at least 1120 bytes of its 3072-byte static stack while
free heap/minimum heap/largest block stayed at 255028/254972/212980 bytes and
free/minimum/largest PSRAM stayed at 8091424/8091424/7995380 bytes. The polling
task and RX8010 reads reported no I²C failures. Physical touch gestures and
Power sleep/wake remain part of the hands-on acceptance checklist below.

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

- With no `m4_batch_v3` value, and with only the retired `m4_batch_v2` value,
  confirm batch 2 is selected. Select each batch from System settings and verify
  the device restarts into the matching direction, edge pattern,
  Full/Fast/Half/window/grayscale behavior, touch calibration, and ghosting.
  Simulate an NVS write failure and confirm the device reports failure without
  restarting or changing the active batch.
- Verify four-corner touch, swipes and rotations, including a short touch while
  an e-paper refresh blocks the main loop; confirm GPIO46 remains unusable and
  that GPIO7 display reset is followed by successful FT6336U reinitialization
  with `0x00=0x00`, `0x80=0x16`, and `0x88=0x04` read back correctly. Confirm
  invalid frames neither create phantom touches nor leave an active touch stuck.
- Verify GPIO1/2 navigation and independent GPIO0 Power input: a short press
  never emits Confirm and follows the existing short-power setting; a long
  press enters sleep.
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
  Wi-Fi cycles. The static touch task must retain at least 512 bytes of stack,
  I²C handles must be allocated only at startup, and no metric may show a
  continuing decline.

AHT20, SC7A20, remote OTA/catalog publication and complex SD fallback remain
outside this experimental target.
