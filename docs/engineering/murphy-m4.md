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

M4 batch detection runs before the normal GPIO input setup. GPIO1/KEY1 has a
100 kΩ pull-up and 100 nF capacitor and serves as the reference channel.
GPIO2/KEY2 has the same network; R13 is a second 100 kΩ pull-up in parallel on
that channel and should halve its charge time on batch 2. The firmware measures
seven paired 50%-charge times in two fixed arrays (56 stack bytes). It confirms
batch 1 only when both medians are 5200–10000 µs and GPIO2/GPIO1 is 75–125%.
Any pressed key, failed discharge, timeout, out-of-range sample, ratio mismatch,
or other result uses the market-default batch 2.

Only a confirmed batch 1 is cached in `cphw/m4_batch_v2`. A cached First value
is restored directly; Second, damaged, and missing values trigger a fresh safe
probe and are not deleted. Batch 2 is never written, so it is rechecked on each
boot and remains the fallback. `HalGPIO::begin()` applies the result to touch
before input starts; `HalDisplay::begin()` reads the same HAL state before
constructing the immutable SSD1677 batch configuration. The probe uses no heap
allocation.
[ESP32-S3 Datasheet v2.2](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
Table 2-8 maps GPIO1 to ADC1_CH0 and GPIO2 to ADC1_CH1; its documented 60 µs
power-up low glitch is also shorter than the 50 ms settle delay before the
charged reference samples.

Batch 1 (no R13) uses the `0x3C` pseudo-temperature and touch short-axis range
`[-52,553]`; batch 2 (R13 fitted) uses `0x50` and `[-47,514]`. The selected
temperature is applied to HALF and window refreshes. For recovery or hardware
diagnostics, `-DFREEINK_MURPHY_M4_BATCH1=1` forces batch 1 and bypasses the
probe. The product UI deliberately has no manual batch setting.

The available GPIO1 reference-channel data is stable: first-batch hardware
measured a 6008 µs median across 101 samples (6004–6059 µs, 0.379% coefficient
of variation), while known second-batch hardware measured 6218 µs
(6170–6233 µs, 101/101 valid). Their overlap proves GPIO1 alone cannot identify
the batch. GPIO2/R13 has not been sampled on hardware, so the production rule
does not attempt to identify batch 2; it only confirms the no-R13 first-batch
topology and otherwise uses batch 2. A first-batch restart log has confirmed
that an existing `m4_batch_v2=First` cache still restores batch 1.

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

- Before changing the first-batch gate, measure paired GPIO1/GPIO2 RC samples
  and verify its absolute and ratio bounds. Confirm only a positive first-batch
  result is cached, a later boot restores that cache, and holding Up or Down
  during an uncached probe cannot persist a result. Verify direction, edge
  pattern, Full/Fast/Half/window/grayscale and ghosting on the second display
  batch.
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

AHT20, SC7A20, manual panel-batch settings, remote OTA/catalog publication and
complex SD fallback remain outside this experimental target.
