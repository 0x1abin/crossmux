# OnePage (壹页) — ESP32-C61, SSD1677 800×480

> Deep reference for [AGENTS.md](../../AGENTS.md).

## TL;DR

OnePage is a 4.26" open-source e-ink reader designed by MoveCall. It uses the
ESP32-C61HR2 (RISC-V RV32IMAC, Wi-Fi 6, BLE 5.0) with 16 MB flash and 2 MB
PSRAM, a GDEQ0426T82 SSD1677 800×480 panel, a 4-key ADC resistor-ladder plus
3 GPIO keys, SPI SD card (shared bus with the display), and a 1000 mAh battery
with ADC voltage sensing and GPIO charge detection.

The FreeInk SDK ships a complete board profile (`FREEINK_DEVICE_ONEPAGE`);
CrossMux adds a build environment and minimal HAL integration.

## Hardware summary

| Area | Value | Source |
|------|-------|--------|
| SoC | ESP32-C61HR2, RV32IMAC 160 MHz, single core | SDK `onepage-c61-support.md` |
| Flash | 16 MB QD | SDK |
| PSRAM | 2 MB SPI @ 40 MHz | SDK |
| Display | GDEQ0426T82, SSD1677, 800×480, 4-level grayscale | SDK |
| Display SPI | SCLK=GPIO22, MOSI=GPIO23, CS=GPIO25, DC=GPIO8, RST=GPIO27, BUSY=GPIO29 @ 20 MHz | SDK |
| SD card | SPI (shared with EPD): SCLK=GPIO22, MISO=GPIO24, MOSI=GPIO23, CS=GPIO26 @ 20 MHz | SDK |
| Buttons | 4-key ADC ladder (GPIO4) + 3 GPIO keys (GPIO6/9/2) | SDK |
| Battery | ADC GPIO5, charge detect GPIO11, charge enable GPIO10 | SDK |
| Audio | PDM microphone (not used by CrossMux reader) | Project page |
| USB | USB-C, Serial/JTAG (native C61 USB) | Project page |
| Wireless | Wi-Fi 6 (802.11ax) + BLE 5.0 | SoC datasheet |
| Physical | 116×66.5×5 mm, CNC aluminium midframe, acrylic back panel | Project page |
| Boot media | Micro SD required — device cannot boot without a FAT32 card | Project page |

## Integration path

Per [port-device-bsp](../../.agents/skills/port-device-bsp/SKILL.md): the SDK
already supports the board → reuse its profile and drivers; add only the
CrossMux build environment and minimal HAL integration.

### What the SDK provides

- `BoardConfig::Board::OnePage` profile with all pin assignments
- `FREEINK_DEVICE_ONEPAGE` compile-time macro
- SSD1677 display driver (same controller as X4, Murphy M4, Waveshare 3.97)
- InputManager configuration for ADC ladder + GPIO keys
- BatteryMonitor ADC and charge-detect pin mapping

### What CrossMux adds

- `[onepage_hardware]` build profile in `platformio.ini`
- `FREEINK_DEVICE_ONEPAGE` in the AfterUSBPower wakeup guard (`src/main.cpp`)
- Standard recovery button (BTN_UP) via the existing recovery path
- No HAL-specific code branches: the SDK profile drives BoardConfig, and the
  HAL's default paths handle display, input, storage and power

### What CrossMux does NOT need

- No `HalDisplay` changes — SSD1677 uses the default `begin()` path
- No `HalPowerManager` changes — battery ADC is BoardConfig-driven; no PMIC
  shutdown sequence; standard CPU frequency scaling works on C61
- No `HalStorage` changes — SPI SD uses the default SdFat path (no SDMMC)
- No `HalTiltSensor` — no IMU on this board
- No `HalClock` external RTC — uses the ESP system clock with NTP sync
- No `HalFrontlight` — no frontlight hardware
- No I²C expander or PMIC bring-up in `HalGPIO::begin()`

## Build environments

```
pio run -e onepage              # Development build with serial log
pio run -e onepage -t upload    # Build + flash via USB
pio run -e onepage-gh_release   # Stable release
pio run -e onepage_nightly      # Nightly release
```

The build uses pioarduino's **hybrid compile** for ESP32-C61 (Arduino as
ESP-IDF component). The `board = esp32-c61-devkitc-1` definition is provided
by pioarduino platform-espressif32.

## Memory budget

| Resource | Size | Notes |
|----------|------|-------|
| Internal RAM | ~380 KB usable | C61 has similar constraints to C3 |
| PSRAM | 2 MB SPI | Framebuffer (48 KB) stays in internal RAM |
| Flash | 16 MB | Dual OTA partition slots (6.25 MB each) |
| Framebuffer | 48,000 B | 800×480 / 8, single buffer mode |

BLE host stack uses PSRAM (`CROSSPOINT_BLE_HOST_PSRAM=1`). The tuned core
rebuild (`firmware_tuned`) drops unused cloud components to reclaim heap.

## Differences from closest reference (Murphy M4)

| Feature | OnePage | Murphy M4 |
|---------|---------|-----------|
| SoC | ESP32-C61 (RV32IMAC) | ESP32-S3 (Xtensa dual-core) |
| PSRAM | 2 MB SPI | 8 MB OPI |
| Display | SSD1677 800×480 (same) | SSD1677 800×480 (same) |
| Input | ADC ladder + GPIO | FT6336U touch + GPIO |
| SD card | SPI (shared with EPD) | SDMMC 4-bit native |
| Power | ADC + GPIO charge detect | GPIO |
| PMIC | None | None |
| Frontlight | No | No |
| RTC | No (system clock + NTP) | No (system clock + NTP) |
| CPU freq scaling | Standard | Bypassed (touch issue) |

## Verification checklist

### Build
- [ ] `pio run -e onepage` compiles without errors
- [ ] `./bin/ci-check` passes
- [ ] Existing X3/X4 shared image unaffected (no HAL changes)

### Physical (pending hardware)
- [ ] Cold boot without panic/OOM; serial log shows `Hardware detect: onepage`
- [ ] SSD1677 full/fast/grayscale refresh, orientation correct, no ghosting
- [ ] ADC ladder keys respond (4 keys: Back, Left, Right, OK)
- [ ] GPIO keys respond (3 keys: Wake/AI, PageUp, PageDown)
- [ ] SD card mount, read/write, EPUB open and page turn
- [ ] Battery ADC reading reasonable; charge detect on USB plug
- [ ] USB insert/remove detection
- [ ] Deep sleep + button wake (≥3 cycles); display/input/SD recover
- [ ] BLE page turner pairing and key delivery
- [ ] Wi-Fi 6 connection test
- [ ] Heap/PSRAM snapshot before/after init and after 30 min reading

## Sources

- Hardware: <https://oshwhub.com/movecall/project_ucjwpaya>
- Firmware (original): <https://github.com/MoveCall/crosspoint-onepage>
- FreeInk SDK board profile: `freeink-sdk/docs/onepage-c61-support.md`
- ESP32-C61 support: ESP-IDF v5.5.2+, pioarduino hybrid compile
