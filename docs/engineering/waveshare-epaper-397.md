# Waveshare ESP32-S3 ePaper 3.97

Experimental hardware target: ESP32-S3, 16 MiB flash,
8 MiB PSRAM, 800×480 SSD1677, 4-bit SDMMC, AXP2101, PCF85063A,
and ES8311/NS4150B audio.

```bash
pio run -e waveshare_epaper_397
pio run -e waveshare_epaper_397 -t upload --upload-port /dev/tty.usbmodem101
pio device monitor --port /dev/tty.usbmodem101 --baud 115200
```

## Hardware contract

| Function | Wiring |
|---|---|
| EPD SPI | SCLK 11, MOSI 12, CS 10, DC 9, RST 46, BUSY 3; 20 MHz |
| SDMMC | CLK 16, CMD 17, D0/D1/D2/D3 15/7/8/18; 4-bit |
| I²C | SDA 41, SCL 42; 400 kHz |
| RTC | PCF85063A at `0x51` |
| PMIC | AXP2101-compatible PMIC at `0x34` (see the TG28 note below); IRQ on GPIO38; ALDO1/2 supply audio and ALDO3 supplies the EPD |
| Audio | ES8311 at `0x18`; MCLK 13, BCLK 14, WS 47, DOUT 48; NS4150B enable 39 |
| Buttons | Back 0, Left 4, Function 5, Right 6; active-low |

### TG28 silkscreen

The PMIC on the shipped board is marked **TG28**, not AXP2101, and the
Waveshare schematic uses that name. Its pins, nets and register behaviour match
AXP2101 at `0x34` (ALDO1-4, BLDO1/2, PWRON, PWROK, AXP_SDA/SCL, EPD_VCC_AXP), so
the AXP2101 driver is used unchanged. The driver never reads the chip ID
register `0x03`, so this target claims **functional equivalence only**: an ID
readback of `0x4A` has not been observed and must not be recorded as if it had.
A future driver that starts validating `0x03` needs a TG28 measurement first.

Function single-click emits Confirm after the 300 ms double-click window. A
second short click starting within that window emits Back. Holding either press
for 300 ms instead holds Confirm from its physical press timestamp, so existing
business long-press actions work and GPIO5 never emits Power.

The side Power key is connected to the AXP2101 rather than a GPIO button. Its
GPIO38 IRQ is mapped to the standard Power input, including the existing short
Power action and hold-to-sleep behavior. Hold it for about one second to power
on; the boot gesture is ignored until its first release, so it may remain held
until the first screen is visible. A new runtime hold uses the existing 400 ms
software shutdown threshold (about 10 ms when short Power is set to Sleep),
while a continuous 4-second hold remains the PMIC hard-power-off fallback.
QMI8658 and SHTC3 are deliberately not initialized by this target.

System Settings exposes **Sound Feedback** with Off/Low/Medium/High levels;
Medium is the default on this target. Low/Medium map to the previous Medium/High
ES8311 volume values 85/100. High keeps codec volume 100 and applies saturating
1.75x PCM gain (about +4.9 dB). Debounced physical Left/Right presses play
`select`; physical Confirm, Back, and Power presses play `tap` immediately,
before click/double-click/hold business classification. A hold produces no extra
cue. The two 16 kHz mono PCM WAVs are embedded only in this build.

The build opts into `CROSSPOINT_CAP_SOUND_FEEDBACK`. `SoundFeedback` owns cue
selection, settings interpretation, RIFF data-chunk parsing, calibration, and
PCM gain; `HalAudioOutput` owns the AudioManager and board power lifecycle; the
FreeInk AudioManager owns codec/I²S streaming. Adding another speaker board
requires a reviewed calibration and capability opt-in, not a copy of the cue
player. The embedded WAV data chunks are located at runtime, so playback does
not assume a 44-byte header.

When a cue is already playing, the next physical press interrupts and replaces
it at the next PCM buffer boundary; input handling never waits for playback.
The I²S line is primed before the NS4150B is raised, followed by a Waveshare-only
10 ms amp-settle interval so the 10 ms `select` waveform is not clipped. Off
stops playback and powers down the codec, amplifier, and AXP2101 audio rails.
The wake gesture remains absorbed. A screenshot chord may play the constituent
physical-key cues, but screenshot recognition adds no cue of its own.

GPIO4/GPIO6 short presses emit Left/Right on release. Holding either key for
650 ms instead emits and holds Up/Down respectively; releasing it produces only
the matching Up/Down release.

The panel uses the shared SSD1677 driver with a Waveshare-specific configuration.
FULL, HALF, and FAST select the controller's `0xF7`, `0xD7`, and `0xFF`
sequences respectively; HALF writes temperature `0x6A`. The shared asynchronous,
shadow-buffer, and window-refresh paths remain enabled. Normal antialiased page
turns first apply the `0xFF` B/W partial baseline, power the analog rails with a
separate `0xC0` activation, then drive only gray selector pixels through the
Waveshare-owned custom LUT and `0xCC`. The LUT initially matches X4 but remains
independent for panel-specific VCOM/VSH1 calibration. This path uses the existing
strip scratch and framebuffer only—no full-screen buffer or heap allocation.

The first page and periodic cleanup may still use HALF and visibly flash. Normal
pages use only the selector path and do not run a four-gray `0xD7`, 500 ms
settle, or hard-reset sequence. Deep sleep sends `0x10/0x01`; the existing
AXP2101 shutdown path then removes system power.

## Physical acceptance gate

- Confirm boot without panic/OOM and successful PSRAM, AXP2101, SDMMC, and RTC initialization.
- Visually check full, fast/windowed, half, and four-gray refreshes; repeat sleep/wake three times.
- Repeat Back, Left, Right, Function single/double/hold, and side Power gestures three times; one gesture must
  produce one action.
- With Sound Feedback at its fresh-install Medium default, confirm physical Left/Right presses play audible `select`
  and Confirm/Back/Power presses play `tap` without waiting for release or gesture classification. Confirm long Power
  plays once at press, while a wake-held Power button remains silent.
- Check Off/Low/Medium/High persistence across reboot and verify the three audible levels are clearly distinct.
- Press buttons rapidly 100 times and switch Sound Feedback levels ten times. Confirm each new cue interrupts the prior
  cue without blocking input, the amplifier does not pop or stutter, and serial free-heap/largest-block readings do not
  trend downward.
- Open an EPUB from SD, turn pages, and confirm progress/settings writes survive reboot.
- Set the RTC, reboot and fully power-cycle, then confirm restored time.
- Check battery percentage and charging; unplug USB, run on battery, shut down, then hold the side key until the first
  screen is visible before releasing it. Repeat three times and confirm the boot gesture never triggers shutdown.
- Confirm USB Serial/JTAG logging and flashing still work after a normal boot. Open File Transfer > USB Drive and
  verify a host can mount the SD card, copy, rename, delete, and read a large file. Safely eject or disconnect while
  idle, then confirm the device reboots Home, remounts the SD card, and opens a transferred EPUB. Repeat three times.
- Enter USB Drive without a connected host, cancel, and confirm the device reboots Home with the SD card mounted.
  Record `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, and `ESP.getMaxAllocHeap()` before entry and after reboot; use
  external UART to record the same values while MSC is active when available.

Automated builds and serial logs do not substitute for the visual, button, or
battery checks above. Record incomplete checks as pending rather than accepted.

### 2026-10-28 local flash session

Board: ESP32-S3, 16 MiB flash, embedded 8 MiB PSRAM, MAC `44:b1:76:b6:a3:bc`.
Repository `0x1abin/crossmux` at `6b5566c8`; the session ran on a topic branch
with no source changes.

- **Backup first.** The complete 16 MiB factory image was read and verified
  before anything was written (`verify OK (digest matched)`, SHA-256
  `34fac2c6ca24bc0f90600e245ee6edfd0b165de90ba92529e6ce0fca1d55bf07`). It is
  kept outside the device as the only full-flash recovery image.
- **Build.** `pio run -e waveshare_epaper_397` took 665.7 s and produced
  `firmware.bin` 6,310,096 B and `firmware.factory.bin` 6,375,632 B. Flash use
  is **96.2 %** of the 6,553,600-byte app partition (6,304,895 B used, 248 KB
  free) and RAM is 34.8 % (114,116 / 327,680 B). The remaining flash headroom is
  the binding constraint on this target.
- **Flashing.** The factory image was written at `0x0` and hash-verified. Use
  esptool **4.8.1** here: 5.3.0 aborted at 7.6 % with
  `Packet content transfer stopped` at both 460800 and 921600 baud.
- **Boot.** No panic, abort or OOM. `AXP2101 ready: battery=1 percent=95
  external=1 charging=1` (97 % after charging), `External RTC found`, SDMMC card
  mounted (128 GB), `Display initialized`, internal heap 168,528 B free with
  7,951,136 B PSRAM free.
- **Display.** First frame 1,682 ms, later full frames 505 ms. Both the text-AA
  and the grayscale path were exercised on real pages. Reader page render was
  1,012 ms for a full refresh and 962-998 ms for windowed updates
  (`changed=70478`), matching the `0xCC` selector design.
- **EPUB.** Cache build 117 ms for 11 spine items; first page 1,154 ms.
  Progress, reading stats and the recent-books list survived reboot.
- **Sleep and wake, three cycles.** Holding Power for 401-404 ms logged
  `Power button held 401ms, sleeping` and entered the Sleep activity; USB power
  disappeared. Each wake restored the external RTC time (22:10 / 22:11), the
  selected theme, settings, reading progress (spine 3), reading stats and the SD
  mount. The 10-minute idle auto-sleep also fired once and woke normally.
- **Wi-Fi.** After wake the device reconnected with saved credentials
  (`RSSI -69 dBm`) and started SNTP without re-entering a password. Internal heap
  during Wi-Fi was 107,008 B free with a 65,524 B largest block.
- **Themes.** Lyra, Lyra Carousel and RoundedRaff all rendered and persisted
  across a reboot.
- **USB Drive.** A host mounted the SD card, and copying a file through it
  succeeded. Note that Home lists *recent books*, not a directory scan, so a
  newly copied book only appears after it is opened once through Browse Files.
- **Sound Feedback was not evaluated** in this session; the user deferred it.
- **Still pending:** Off/Low/Medium/High audibility and persistence, the
  100-press heap trend, battery-only operation and shutdown, and a repeat of the
  USB Drive copy/rename/delete/eject cycle three times. Hosted CI and release
  publication were not run from this session.
