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
| PMIC | AXP2101 at `0x34`; IRQ on GPIO38; ALDO1/2 supply audio and ALDO3 supplies the EPD |
| Audio | ES8311 at `0x18`; MCLK 13, BCLK 14, WS 47, DOUT 48, DIN 21 (mic); NS4150B enable 39 |
| Buttons | Back 0, Left 4, Function 5, Right 6; active-low |

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

## Voice Notes (microphone capture)

The board's analog microphone feeds the ES8311 ADC, whose serial output
(ASDOUT) is wired to GPIO21. BoardConfig only describes the playback path
(`NO_MIC`, and `freeink::Microphone` is PDM-only), so `CROSSPOINT_CAP_VOICE_RECORDER`
enables a CrossMux-side `HalMicrophone` that keeps the DIN pin locally. Moving
it into the SDK as an I2S-codec `MicConfig` is a follow-up.

`HalMicrophone::begin()` shuts down `HalAudioOutput` (freeing `I2S_NUM_0` and
the codec), raises the AXP2101 audio rail, starts an I2S RX channel at 16 kHz
with MCLK = 256·fs, then resets the ES8311 and programs the ADC path (analog
mic, 24 dB PGA, DAC muted). `end()` powers the ADC down and drops the rail; the
next feedback cue re-initializes playback on its own. While capture is active
the main loop skips `SoundFeedback::update()`, so button cues neither reopen
I²S TX nor end up in the recording.

The Voice Notes app records from a FreeRTOS task pinned to core 0 (priority 5,
6 KB stack) into `/recordings/RECnnnn.wav`; 8×512-frame DMA buffers give
256 ms of slack against SD write stalls. Transcription streams the WAV to
`api.openai.com/v1/audio/transcriptions` (`whisper-1`) over the SDK's wolfSSL
`SecureClient` in 4 KB chunks, so peak extra RAM is one chunk plus the TLS
session. The API key is set on the web Settings page under Voice Notes. Wi-Fi
comes up only for a user-started transcription and is turned off afterwards if
the app started it. File layouts are in `docs/file-formats.md`.

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

- Voice Notes: record 30 s, then copy `/recordings/REC0001.wav` to a computer and confirm clean 16 kHz mono audio of
  the right length. Record again and pull power mid-recording; the file must still play, short by at most 2 s. Press
  buttons while recording and confirm no cue plays or is captured, then confirm cues return after leaving the app.
  Let a recording hit the 12-minute limit once.
- Voice Notes transcription: set the OpenAI key on the web Settings page, reload, and confirm only the mask is shown.
  Transcribe from the app with Wi-Fi off, confirm `REC0001.txt` opens in the reader and Wi-Fi is off afterwards.
  Repeat with a wrong key (OpenAI's error is shown), with Back during upload (cancels), and with the access point
  switched off mid-upload (clean error). Record free heap and largest block before and after five
  record-and-transcribe cycles; they must not trend downward.

Automated builds and serial logs do not substitute for the visual, button, or
battery checks above. Record incomplete checks as pending rather than accepted.
