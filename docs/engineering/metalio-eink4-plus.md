# Metalio E-Ink4 Plus development target

The beta port starts at CrossMux `b0641c5b30b7d0ae8f0fc4fa3d2d1c5d769214d2`.
It carries the board changes from [CrossPoint Plus `ea534874`](https://github.com/crosspoint-reader/crosspoint-reader/tree/ea534874bd60074058b8862c0f57bf59def1f0ee)
and [FreeInk SDK PR #166 `99901125`](https://github.com/Free-Ink/freeink-sdk/pull/166)
on the CrossMux SDK fork `0af7bb68e1e64939ed92663464fbd03f02329c6a`.
The SDK candidate pins [EPDiy `edc78157`](https://github.com/Free-Ink/epdiy/commit/edc78157fdf20d0ccff405b6e5164d0663acd803).
The CrossMux submodule pins the reviewed FreeInk SDK revision.

## Board and reader

- ESP32-S31, 16 MiB Flash, 16 MiB octal PSRAM, 1216 × 684 E0470A14-AF on the
  8-bit LCD bus and Plus-specific bezel insets. Inx uses the native high-DPI
  layout. The other themes use the upstream 1.5× UI scale, as on Read Pico.
  The battery glyph and percentage share the scaled text metrics.
- Sleep images use the original Read Pico layout in native pixels. The panel
  gets a full white refresh before a replacement image. Quick Resume and
  transparent overlays retain the previous frame.
- Fast uses the reinforced GC16 waveform. Half drives all pixels and Full
  cleans before the redraw. Native gray transactions borrow the EPDiy front
  buffer, even pixel in the low nibble, 0 black and 15 white. The reader and
  image/cover paths use all 16 drive values. A failed waveform invalidates the
  optical baseline so the next draw clears from white. The 4-bpp front and
  back plus the 8-bpp difference buffers occupy 1,663,488 bytes of PSRAM. Each
  lazy selector plane uses 103,968 bytes of PSRAM.
- FT6336U uses the beta's touch queue and acquisition timestamps. It falls
  back to synchronous sampling if queue or task creation fails. A screen
  change drops queued input and suppresses inherited contacts. An invalid
  sample or zone change cancels the gesture until confirmed release.
- The CX25601N charger reports USB presence. Frontlight uses the vendor PWM
  settings and a 10% duty cap. The S31 runs at 320 MHz to feed the EPDiy line
  queues. Power-off pulses the expander and falls back to GPIO2 deep sleep.
  The fallback checks the power key before accepting an expander wake.
- SDMMC stays at 1-bit/20 MHz because the previous same-model 4-bit write
  tests failed. S31 Wi-Fi station startup disables modem sleep and limits
  protocols to 802.11b/g/n. The board caps transmit power at 15 dBm.

## Bluetooth and USB

The S31 links the prebuilt ESP-IDF NimBLE host and the pinned NimBLE-Arduino
2.5.1 C++ wrapper. The candidate uses the upstream S31 RAM allocation and
admission policy. A running stack may stay up during a chapter build only
when the S31 policy's free and contiguous heap thresholds are satisfied.
The existing network startup policy still releases BLE before starting Wi-Fi.

USB Drive uses the existing SD block-device handoff. The SDK routes Type-C to
the OTG PHY through GPIO36 while the host owns the card. On teardown it
disconnects MSC before restoring the USB Serial/JTAG route. The S31 build
links a trimmed TinyUSB archive containing MSC and the required device core,
removing the unused class implementations and USB host stack.

## Build and acceptance

```sh
git submodule update --init --recursive
PLATFORMIO_CORE_DIR=/tmp/crossmux-plus-beta-pio PLATFORMIO_BUILD_DIR=.pio/build-s31 pio run -e metalio_eink4_plus
python3 -m unittest discover -s scripts/tests -p test_metalio_eink4_plus.py
```

Use PlatformIO Core 6.2+ and pioarduino 61.04.00-RC1. The S31 build keeps its
Core directory separate from the other target's ESP-IDF toolchain. Build
checks cover the waveform, framebuffer transaction, touch sampling and
cross-screen cancellation, startup logs, USB routing and eject, radio admission
and target isolation.
The target is for local development.

The upstream beta needs a bootloader from the same ESP-IDF build to avoid
LCD feed underruns. The November 10 evidence retains the two matching 16 MiB
flash backups and the original NVS. One historical candidate damaged NVS
namespace metadata on page 0. Its root cause remains open.

## Hardware acceptance before the integration refactor

The candidate was tested on an ESP32-S31 revision 0.0 with 16 MiB flash.
The live bootloader matched the candidate's ESP-IDF v6.1-803-g94639ab7251
bootloader. Only app0 at 0x10000 was written, with independent readback.
The bootloader, partition table and OTA data matched the pre-write snapshot
after testing.

The user confirmed display orientation and readability, touch page turns
in both directions, sleep and wake, and a physical BLE page turner. The
serial log captured the reader reopening and BLE reconnecting after reboot.
macOS mounted the SD card as `/Volumes/NO NAME`, read a 5,639,348-byte EPUB,
and ejected the volume. The reader remounted the SD card and resumed. The
user confirmed the volume opened in Finder.

The post-runtime NVS passed the ESP-IDF integrity check. The active records
changed in `freeink-hid`, `nimble_bond` and `cptime`. NVS page 0 matched
the pre-write snapshot. Sleep current has yet to be measured.

## Earlier S31 probes and factory recovery

[ar0c's BSP probe PR #465](https://github.com/0x1abin/crossmux/pull/465)
is a separate bring-up path. See the
[pinned probe](https://github.com/ar0c/crossmux/blob/a43d02ff799ed330a47cd3606ec9485400f7c8fb/src/debug/MetalioEink4PlusPlatform.cpp)
and [trial and recovery notes](https://github.com/ar0c/crossmux/blob/a43d02ff799ed330a47cd3606ec9485400f7c8fb/docs/engineering/metalio-eink4-plus.md).
The full-reader trial was on a separate contributor branch, not the probe PR.
That reader booted with 16 MiB PSRAM, but the display showed horizontal bands.
Restoring the verified full 16 MiB factory backup cleared the bands and restored
touch on stock firmware 2.0.79.

The stock 5 MiB app slots do not fit the current development app. The probe,
bench layout and restore evidence are historical references, not first-install
instructions or acceptance of the current EPDiy build. The probe's 4-bit SDMMC
assumption does not override the current 1-bit configuration. Track public
installer and recovery validation in [#333](https://github.com/0x1abin/crossmux/issues/333).
