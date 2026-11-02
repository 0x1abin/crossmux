# OnePage ESP32-C61

Status: **bring-up in progress; Windows build blocked**. No firmware image
exists yet, so nothing has been flashed and no physical acceptance has run.
Build and physical acceptance are recorded separately below; a successful
compile is not hardware acceptance.

Scope decision (2026-11-01): the first slice is the basic reading link —
display, seven keys, SD, battery/charging, and deep sleep/wake. BLE page-turner
support is deferred until that slice is physically accepted.

## Hardware contract

| Area | Fact | Evidence |
|---|---|---|
| Board | OnePage V1, 2026-08-19 | [OSHWHub project](https://oshwhub.com/movecall/project_ucjwpaya), [`electronics/c61/README.md`](https://github.com/MoveCall/onepage-reader/tree/main/electronics/c61) |
| SoC | ESP32-C61HR2, 160 MHz RISC-V, Wi-Fi 6, BLE 5.4 | [OnePage site](https://movecall.github.io/onepage-reader-web/zh/), [board JSON](https://github.com/MoveCall/crosspoint-onepage/blob/main/boards/onepage-c61.json) |
| Memory | 16 MB flash, 2 MB PSRAM at 40 MHz | [`boards/onepage-c61.json`](../../boards/onepage-c61.json), `CONFIG_SPIRAM_SPEED_40M` |
| Display | 4.26 inch, 800 x 480, SSD1677, SPI, 4-level grayscale | [`freeink-sdk/docs/onepage-c61-support.md`](../../freeink-sdk/docs/onepage-c61-support.md) |
| Display pins | SCLK 22, MOSI 23, CS 25, DC 8, RST 27, BUSY 29; 20 MHz | Same SDK profile |
| SD | SPI on the display bus: SCLK 22, MISO 24, MOSI 23, CS 26; unswitched rail; `SD_DEP` card detect on GPIO28 | V1 schematic and SDK profile |
| Front keys | Four keys on one ADC ladder, GPIO4: Back, Left, Right, Confirm | Same SDK profile; physical order confirmed by the vendor `MappedInputManager` |
| Side keys | Up GPIO6, Down GPIO9, Power GPIO2 (deep-sleep wake) | Same SDK profile |
| USB / charge | USB detect GPIO11, LM66200 ST open-drain, LOW = cable present; charge pause GPIO10 | Same SDK profile; vendor hardware notes |
| Battery | 1000 mAh, ADC GPIO5, divider x2.0, no fuel gauge | [OnePage site](https://movecall.github.io/onepage-reader-web/zh/) and SDK profile |
| Power | GPIO27 = EPD reset only; GPIO10 = charger enable; GPIO11 = USB detect | V1 schematic; `CHARGE`/`PSW` blocks |
| Absent | Touch, frontlight, RTC, IMU, audio, LEDs | [OnePage site](https://movecall.github.io/onepage-reader-web/zh/); vendor boot skips IMU/RTC probes |

The schematic PDF is published as
[`SCH_Sch_OnePage_V1_2026-08-19.pdf`](https://github.com/MoveCall/onepage-reader/blob/main/electronics/c61/SCH_Sch_OnePage_V1_2026-08-19.pdf).
The V1 schematic was rendered during bring-up. It confirms that GPIO27 is
`EPD_RST` only and that `SD_DEP` on GPIO28 is the card-detect switch; it does not
show a switched SD rail. U12 (LM66200) `ST` and U3 charger `CE` behavior still
need to be checked against their datasheets before treating their polarity as
measured.

## Selected route

- Keep OnePage as a separate C61 image. It is not part of the shared X3/X4 C3
  image and does not inherit the S3 allocator or radio profile.
- Reuse the pinned FreeInk `ONEPAGE` `BoardProfile`, SSD1677 driver, ADC-ladder
  input, SPI SD, and ADC battery backend. Add only the CrossMux build/HAL glue.
- Add `boards/onepage-c61.json` and the `onepage` environment with the
  `partitions_onepage.csv` layout read from this unit.
- Start with 40 MHz flash. The pinned pioarduino platform ties PSRAM speed to
  `f_flash` for non-P4 chips, and the C61HR2 eco3 needs PSRAM at 40 MHz. Raising
  flash to 80 MHz requires decoupling the platform or hardware validation of the
  resulting PSRAM clock.
- Skip the IMU and external-RTC probes at boot. The vendor port documents that
  probing absent I2C devices on the C61 strap pins can hang boot.
- Keep the four front-button hints in physical order: Back, Left, Right,
  Confirm. The generic X4 order is Back, Confirm, Left, Right.
- Read the USB-detect pin with the polarity declared by `BoardProfile`. The
  pinned SDK profile initially left `usbDetectActiveHigh` at its `true` default
  even though GPIO11 is LOW when USB is present; the SDK fix is tracked
  separately and the CrossMux gitlink must stay on the committed revision until
  that fix lands.

## Revisions and reproduction

| Item | Revision |
|---|---|
| CrossMux base | `6b5566c844667857acebfe565d373423cdd16e06` |
| CrossMux bring-up commit | `abd991a` on `codex/port-device-bsp` |
| FreeInk SDK gitlink | `65323afd0cbfa7bcfbabafd509248d024ceaab14` (unchanged) |
| FreeInk SDK local branch | `codex/onepage-c61` at `ceeca4b` (USB polarity, SD rail, EPD reset, C61 library-pass device default); not yet in the gitlink |
| Toolchain | pioarduino Core 6.2.0, Espressif32 platform 55.3.37 for `onepage` |
| Board definition | `boards/onepage-c61.json` |

The `onepage` environment overrides the platform to 55.03.37 because 55.3.311
regressed the Arduino/IDF hybrid pass on C61: its final link only saw the
`.dummy` placeholder, so `setup()`/`loop()` were missing. 55.03.37 is the
platform the vendor C61 port validates with the same board and partition
layout. Other CrossMux targets stay on the base pin.

```bash
pio run -e onepage
```

The local toolchain is installed outside the repository. On this Windows host
the minimum invocation is:

```powershell
$env:PYTHONPATH = "<workspace>\.tools\pio-packages"
$env:PLATFORMIO_CORE_DIR = "<workspace>\crossmux\.platformio"
$env:UV_CACHE_DIR = "<workspace>\.tools\uv-cache"
$env:IDF_COMPONENT_CACHE_PATH = "<workspace>\.tools\idf-component-cache"
$env:IDF_TOOLS_PATH = "<workspace>\.tools\espressif"
$env:TEMP = "<workspace>\.tools\tmp-build"
$env:TMP = $env:TEMP
$env:IDF_SKIP_CHECK_SUBMODULES = "1"
& "<workspace>\.tools\pio-packages\bin\pio.exe" run -e onepage
```

`IDF_SKIP_CHECK_SUBMODULES=1` is required on this Windows layout because
ESP-IDF's CMake helper sees the already-present `freeink-sdk` submodule as
missing and tries to clone its nested `lucide` submodule. It is the upstream
IDF escape hatch, not a source change. `UV_CACHE_DIR` and `TEMP` must point
inside the workspace because the host sandbox denies the user-profile cache.

Current Windows blocker (2026-11-02): the hybrid parent pass compiles every IDF
and project library, but the final link uses `.dummy` as the project source, so
`framework-arduinoespressif32.a(main.cpp.o)` cannot resolve `setup()`/`loop()`.
The `src` objects are never compiled because the hybrid pass replaces
`PROJECT_SRC_DIR` with `.dummy` and the delegate `pio run` pass then links the
same placeholder. Linux CI is the intended reproducible build; the Windows
host additionally needed local-only platform patches (command-line launcher for
`ldgen`, `SCons.Tool.__path__` fallback, `NodeList` handling, keeping `-D`
flags in the hybrid pass). Those patches live under `.platformio/`, not in the
repository, and are not part of the PR.

Do not flash until the device, image, partition offsets and a verified backup
are recorded in this document.

## Device under test

| Field | Value |
|---|---|
| USB port | `COM9` (USB-Serial/JTAG, VID:PID `303A:1001`) |
| Chip | ESP32-C61HR2 rev v1.0, 160 MHz, 2 MB embedded PSRAM |
| Flash | 16 MB, manufacturer `ef`, device `4018` |
| MAC | `d8:85:ac:60:e4:28` |
| Factory layout | app0 `0x10000 + 0x6C0000`, app1 `0x6D0000 + 0x6C0000`, spiffs `0xD90000 + 0x260000`, coredump `0xFF0000` |
| Installed app0 | CrossMux `nightly-build-83b0f810c86287014`, built 2026-10-18 01:46:10, IDF 5.5.5 |
| Full backup | `device-backups/onepage-c61-D885AC60E428-20261101-full.bin` |
| Backup size / SHA-256 | `16,777,216` bytes / `BF9B62EF6C64C79DC1EBA8E30F59A50663647A5E3F0EEE742DF1523A554C69AE` |

The backup was read from `0x0` to `0x1000000` at 460800 baud and the device was
hard-reset afterwards. No write or erase was performed. The backup lives outside
the CrossMux worktree and must not be committed.

## Flashing plan (not executed)

- Target: the unit at `COM9`, MAC `d8:85:ac:60:e4:28`; do not flash any other
  board.
- Image: `.pio/build/onepage/firmware.bin` (application only, linked for the
  `partitions_onepage.csv` layout).
- Write at `0x10000` only. The current bootloader, partition table, NVS,
  otadata, app1 and SPIFFS stay untouched.
- Verify the written application with `esptool verify_flash 0x10000`.
- Recovery: restore the full 16 MB backup at `0x0`; see
  `device-backups/README.md`.
- Status: **authorized (user: 烧录吧)** once a verified image exists; not
  executed yet. The write needs the built `firmware.bin` and a re-enumerated
  `COM9`.

## Verification

| Check | Target / revision | Status | Evidence |
|---|---|---|---|
| Compile `onepage` | CrossMux branch + SDK working tree, platform 55.03.37 | failed | libraries compile; final link only has `.dummy`, `undefined reference to setup()/loop()` |
| Host/unit checks | CrossMux branch | pending | not run |
| Display, input, SD, battery | OnePage V1, exact unit | blocked | no firmware; device not enumerated at last check |
| Sleep/wake x3 | OnePage V1 | pending | requires device |
| BLE page turner | OnePage V1 | not applicable (first slice) | deferred by scope decision |
| Current measurement | OnePage V1 | pending | no measurement |

## Open items

- The ESP32-C61HR2 identity and 16 MB flash are confirmed by esptool. The PCB
  revision marking still needs a visual check against the V1 2026-08-19
  schematic.
- A full 16 MB backup now exists and is hash-recorded above. Flashing still
  requires explicit user authorization and a final image/offset plan.
- Confirm whether the BLE page-turner and full CrossMux feature set are in
  scope for this port.
- Confirm U12 `ST` (USB detect) and U3 `CE` (charge enable) polarity against
  the populated board. The schematic confirms the pin assignments but not the
  populated strapping or the exact charger variant.
