# reTerminal Sticky V1 bring-up

Updated 2026-11-05 (America/Los_Angeles). This record follows one V1 unit against
the existing Sticky support. Historical tests on other units do not establish
acceptance of this source revision on this unit.

## Current state

Reuse the existing SDK profile and CrossMux `sticky` environment. No driver/HAL
change is justified by current evidence. The focused display host suite passed;
the firmware build and offline image checks passed. The approved image is installed;
normal wake and physical acceptance remain pending. The contribution is open as
[Draft PR #458](https://github.com/0x1abin/crossmux/pull/458).

## Hardware contract and evidence

| Area | Evidence and unresolved facts |
|---|---|
| Unit | User confirms reTerminal Sticky **V1**, initially factory firmware, Windows 11 USB connection and installed microSD |
| Recovery | User confirms an existing **verified factory Flash backup** and prefers to manage its path privately; do not request the path or another backup again |
| USB | Windows/pyserial: CH343, COM14, VID:PID `1A86:55D3`, bridge serial `5CBC444239` |
| Silicon | ROM: ESP32-S3 QFN56 rev v0.2, embedded 8 MB PSRAM, 40 MHz crystal; unit MAC `c4:9e:7e:18:ee:14` |
| Flash | ROM ID `ef/4019`: 32 MB, quad, 3.3 V eFuse setting |
| Security | ROM: Secure Boot and Flash Encryption disabled; no eFuses changed |
| Electrical evidence | Supplied [Seeed schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf), Rev 01/V01 dated 2026-06-05. Pages 5–10 visually inspected against SDK wiring; display/SD shared SPI, touch and rail controls, buttons and latch match |
| Display/touch | SDK profile selects 800×480 SSD1677 and GT911; configured identity/mapping is source evidence, **not a fresh panel/controller identification or optical measurement** |
| Power/storage | Existing profile provides SD rail, display/touch rails, power latch and BQ27220-related support; actual SD format/readability, battery behavior, timing and currents remain unknown |
| Reading fixture | Original `CrossMux-Acceptance-Reader.epub` supplied for manual SD copying; numbered passages and four grayscale source levels; on-device reading pending |
| Optional peripherals | Sensor/audio/BLE acceptance not separately requested. Their electrical configuration is not a measurement; no optional-peripheral success claimed |

Requested baseline: boot, legible reading, display/touch/buttons, SD, EPUB page
turns and saved progress, charge/battery handling, shutdown and sleep/wake.
Missing measurements remain pending: panel behavior, refresh/rail timing, internal
heap/minimum/largest block, PSRAM usage and operating/standby current.

## Source provenance and selected route

| Item | Revision / configuration |
|---|---|
| CrossMux origin / PR base | `https://github.com/0x1abin/crossmux.git`, `main` |
| Starting firmware source | `14bd8fb5990cfa24238c7b9242ddd9ab87591e2e` |
| Feature branch | `codex/reterminal-sticky-bringup` |
| Committed SDK gitlink / checkout | `65323afd0cbfa7bcfbabafd509248d024ceaab14`, unchanged |
| Configured SDK repository | `https://github.com/0x1abin/freeink-sdk.git`, from `.gitmodules` |
| Nested lucide checkout | `c81680e066f45b640743ca78ae36cdedda3f0318` |
| Firmware version setting | `2.0.0`; verified dev image `2.0.0-dev-codex/reterminal-sticky-bringup-d311882` |
| Build pins | pioarduino core **6.2.0** (outer and nested), platform **55.03.311**, Arduino **3.3.11**, ESP-IDF **5.5.5**, Xtensa GCC **14.2.0** |
| Host tools | CPython **3.14.5**, clang-format **21.1.8**; Windows venv also has CMake **4.4.4** and Ninja **1.13.2**, platform supplies its own CMake **4.0.3** / Ninja **1.13.1** |

Fresh recursive clone started clean; origin and revisions verified. User work was
not overwritten. Read CrossMux `AGENTS.md`, its port-device-bsp skill, SDK
`AGENTS.md`, SDK device instructions, repository/build/test workflows and PR template.

The route is grounded in:

- `platformio.ini:516–545`: existing separate ESP32-S3 Sticky hardware/dev profile,
  including `FREEINK_DEVICE_STICKY`; do not add it to the C3 X3/X4 runtime enum.
- `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:1529–1604`:
  existing display, touch, shared SD bus, buttons, gauge, sensors and power profile.
  Pending/historical comments must be reconciled with real observations.
- `lib/hal/HalGPIO.cpp:460`: Sticky is already integrated into HAL device behavior.
- `src/main.cpp:836–837`: board rails are held at the start of setup.

Current contribution diff is documentation and its engineering-index link only.
Runtime resource delta is zero: no new allocation, task, driver or firmware data.
Baseline runtime measurements remain pending. No SDK PR is needed for this diff;
if a demonstrated SDK gap appears, use a separate SDK PR and retain the committed
gitlink until its integration, per the porting skill.

## Factory partition boundary and recovery

ROM-only operations reset to the bootloader, read identity/security and 4 KiB at
`0x8000`, then hard-reset factory firmware. No Flash was changed. The captured
partition table's stored MD5 is valid. This capture is not a replacement backup.

| Partition | Factory offset / size | CrossMux offset / size |
|---|---|---|
| NVS | `0x9000 / 0x7d000` | `0x9000 / 0x5000` |
| otadata | `0x86000 / 0x2000` | `0xe000 / 0x2000` |
| phy_init | `0x88000 / 0x1000` | absent |
| app0 | `0x90000 / 0x600000` | `0x10000 / 0x640000` |
| app1 | `0x690000 / 0x600000` | `0x650000 / 0x640000` |
| sys_storage / spiffs | `0xc90000 / 0x930000` | `0xc90000 / 0x360000` |
| usr_storage | `0x15c0000 / 0xa00000` | absent |
| coredump | `0x1fc0000 / 0x40000` | `0xff0000 / 0x10000` |

CrossMux's committed 16 MB layout fits the larger Flash, but an app-only write at
CrossMux's app offset is incompatible with the installed factory layout. Do not
flash until the built image, hash, generated offsets, initialization/erase scope,
recovery procedure and user authorization are concrete. No migration is authorized
merely by the USB/backup confirmations.

Recovery uses the user's verified raw factory backup over the same CH343/ROM
connection; the filename stays user-substituted. Before overwrite, verify the
restore command's offset/size against that backup format and the 32 MB device.
An image and exact restoration command have not yet been selected/executed.

## Local reproduction and environment history

Checkout: `D:\Codex\reterminal-sticky-20261105`.
Tools/logs/materials: `D:\Codex\reterminal-sticky-tools-20261105`.
User approved moving to D: after C: had only about 22 MB free. The cross-drive
move reported an access error but finished with clean tracked files and matching
CrossMux/SDK/lucide revisions; the engineering record survived. No unrelated
files or remotes were replaced, and no user cache was deleted.

Setup history:

1. Initial C: dependency install failed with Windows error 112. D: venv setup and
   required Python dependencies then succeeded. `python-freeze.txt` records them.
2. Nested core initially resolved to `6.2.0.post1`; explicitly pinned to `6.2.0`
   before building, verified by `pio --version`.
3. Optional package inventory and first build attempt hit a `cp1252` Unicode output
   error. Preserve the pin; set Python UTF-8 output instead. First attempt stopped.
4. UTF-8 build reached ESP-IDF component preparation, then failed with no space
   because Component Manager has its own C: cache. Its installed settings source
   exposes `IDF_COMPONENT_CACHE_PATH`. Only the eight entries created by this
   build were moved to D:; the older shared cache entry was left alone.
5. Current retry verifies `FileCache().path()` points to D: before running.

Set these in each PowerShell build process (no global environment changes):

```powershell
$taskTools = 'D:\Codex\reterminal-sticky-tools-20261105'
$env:PATH = "$taskTools\venv\Scripts;$env:PATH"
$env:PLATFORMIO_CORE_DIR = "$taskTools\platformio"
$env:UV_CACHE_DIR = "$taskTools\uv-cache"
$env:UV_PYTHON_INSTALL_DIR = "$taskTools\uv-python"
$env:IDF_TOOLS_PATH = "$taskTools\idf-tools"
$env:IDF_COMPONENT_CACHE_PATH = "$taskTools\component-cache"
$env:MPLCONFIGDIR = "$taskTools\matplotlib"
$env:TEMP = "$taskTools\temp"
$env:TMP = $env:TEMP
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
Set-Location 'D:\Codex\reterminal-sticky-20261105'
pio run -e sticky
```

First-time setup already completed: create tools venv with `uv venv --python
3.14`, install `pioarduino==6.2.0` and repository requirements; run
`python scripts/patch_pioarduino_cache.py --prepare-platform`, then `pio pkg
install -e sticky`; pin the nested core with `uv pip install --python
"$taskTools\platformio\penv\Scripts\python.exe" pioarduino==6.2.0`.
Do not reinstall or clean these caches before ordinary rebuilds.

The focused SDK check ran through WSL's existing compiler, with temporary binaries
on D: to avoid filling its C:-hosted root filesystem:

```powershell
wsl -d Ubuntu-24.04 -- env TMPDIR=/mnt/d/Codex/reterminal-sticky-tools-20261105/temp python3 /mnt/d/Codex/reterminal-sticky-20261105/freeink-sdk/libs/display/FreeInkDisplay/test/host/test_sticky_combined_aa.py
```

## Verification ledger

| Check | Target/revision | Status | Evidence / next action |
|---|---|---|---|
| Repository and recursive dependencies | Revisions above | passed | Clean start, origin and individual HEAD checks |
| Hardware identity/security | Actual V1 unit | passed | `flash-identity.log`, `security-info.log`; ROM reads, no Flash writes |
| Factory partition capture | Actual V1 unit | passed | `factory-partition-read.log`, `materials/factory-partitions.json`; MD5 valid |
| SDK display host checks | Pinned SDK above | passed | 4 tests in 331.775 s, exit 0, `sticky-display-host.log`; production drivers/recording bus, text-turn parameters and image/text routing |
| Original test EPUB integrity | Local test fixture | passed | ZIP CRC and XML checks; source gray levels are not optical calibration |
| Firmware build | `sticky`, d311882 / pinned SDK | passed | Exit 0, 55 min 27 s including custom core; `sticky-build.log` |
| Code-change regressions / `./bin/ci-check` | Current documentation-only diff | not applicable | Repository workflow exempts documentation-only changes; reassess if code changes are needed |
| Documentation whitespace/local links | Current diff | passed | 37 local links present; staged `git diff --check` passed; commit hook passed |
| Factory backup available | Actual unit | passed | User explicitly confirmed verified backup; path remains user-managed |
| Image/offset/recovery plan and overwrite authorization | Actual V1 unit | passed | User explicitly approved the exact image and 0x0–0xFFFFFF write in chat |
| Flash and hash verification | Approved d311882 image, V1 unit | passed | Full write-time hash verified; post-boot all bytes outside mutable NVS/OTA metadata verified separately |
| Boot/display/orientation/touch/buttons | Actual unit and final image | pending | Real logs/visual/button observations needed |
| SD/EPUB/progress/settings, missing-SD behavior | Actual unit and final image | pending | Copy fixture; test reading and persistence |
| USB/battery/charge/shutdown, at least 3 sleep/wake cycles | Actual unit and final image | pending | Real observations and post-wake peripheral checks |
| Internal heap/minimum/largest block and PSRAM | Actual unit | pending | Capture before/after init and repeated reading/sleep; investigate continuing decline |
| Operating/standby current | Actual unit | pending | No measurement or power-saving claim |
| Optional peripherals | Not separately requested | not applicable | No optional-peripheral acceptance claimed |
| SDK contribution | No SDK changes | not applicable | Existing gitlink retained |
| Publication | Draft PR #458 | passed | Open against CrossMux main from the personal fork; readiness remains pending |

## Contribution and next actions

GitHub CLI 2.103.3 authenticated as `neilnorthrop`. Personal fork
`https://github.com/neilnorthrop/crossmux` is remote `fork`; `origin` remains the
CrossMux maintainer repository. No maintainer coordination has occurred or been
claimed. The current PR template's stock-firmware comparison remains unverified;
document the existing port and this unit's evidence without claiming new support.
AI use must be disclosed. Do not merge or publish firmware.

Next: collect normal power-button wake and screen observations, then reading/input/persistence and power-cycle results. Keep the PR Draft for any pending/failed validation or SDK
dependency. Resume from this record and the actual worktree, not historical
claims in other device documents.

### Contribution checkpoint

Draft PR: https://github.com/0x1abin/crossmux/pull/458. Verified `draft=true`,
base `main`, head `neilnorthrop:codex/reterminal-sticky-bringup`. Initial
contribution commit: `d311882fce5350519fd2f83e34af20aeff45b3e3` (documentation
only; runtime sources remain identical to the starting firmware source).

Commit preparation encountered CRLF in the Windows checkout's Bash scripts, then
the Windows `bash` launcher selected WSL, which could not find the Windows formatter.
The unchanged `.githooks/pre-commit` and `bin/clang-format-fix` were restored to their
committed LF bytes locally. Task-local `shell-shims/bash` routes to Git's bundled
Bash-compatible `sh`; PATH includes that shim, the bundled Git `usr/bin`, and the
D: venv Scripts directory. The actual hook ran with clang-format 21.1.8 and passed.
Local shell files were then restored to normal checkout bytes; no shell-script
change is in the PR. Repository-local author uses the authenticated GitHub name
and `2042787+neilnorthrop@users.noreply.github.com`; no global identity was changed.

The custom-core build began before the documentation commit. The outer application
stage reran version injection after that commit and selected d311882; the final
image was verified to contain that exact CrossMux version marker. Runtime sources
match the starting 14bd8fb revision outside documentation. Physical acceptance
remains pending irrespective of build success or Draft publication.

### Successful build and checked first-install artifact

`pio run -e sticky` completed with exit 0. Full elapsed time 55:27 included the
first tuned-core rebuild; the application stage reported 34:54. No runtime/SDK
source fix was needed. The final image passed the repository packaging verifier
for ESP32-S3 chip ID 9, `sticky` board tag, exact version marker and partition CSV /
binary, plus esptool image checksum and validation hash checks.

- Application: **6,224,480 bytes**, SHA-256
  `c21ef9f90ba7dc36d13e6e52f10a21085cef803f5361566fd0e08f52e16e542d`.
- App slot: **6,553,600 bytes**; binary headroom **329,120 bytes**.
- Linker load-size report: Flash **6,219,267 bytes**; static RAM **80,956 bytes**
  of PlatformIO's reported 327,680-byte budget. These are build figures, not
  measured free/minimum/largest-block heap or PSRAM consumption.
- ESP header: ESP32-S3, 16 MB, DIO, 80 MHz. Physical chip is 32 MB; the existing
  CrossMux layout uses the first 16 MB. CrossMux UI version is
  `2.0.0-dev-codex/reterminal-sticky-bringup-d311882`. The separate IDF application
  descriptor reports `nightly-build-582d88b00785972a0`; this does not change the
  selected development build mode.
- Retained nonfatal warnings: repeated `NO_WOLFSSL_ESP32_CRYPT_RSA_PRI`
  redefinition in the pinned wolfSSL configuration, and serial execution of 76
  LTO partitions. No unrelated source changes were made to suppress warnings.
- Current GitHub snapshot: **Title Check passed**. No firmware CI pass is inferred
  from that metadata-only check.

A private padded image was prepared at
`D:\Codex\reterminal-sticky-tools-20261105\images\sticky-d311882-baseline\crossmux-sticky-v1-d311882-full-16MB.bin`.
Length **16,777,216 bytes**, write offset **0x0**, SHA-256:
`5d73c8b81e85107c70fbd5ff70e0695bcbd724b4d08c1bb9494f011e8af4e717`.
`manifest.json` in the same folder records all segment hashes and revisions.

Segments are bootloader `0x0` / 18,720 bytes, partitions `0x8000` / 3,072 bytes,
OTA initializer `0xE000` / 8,192 bytes and app `0x10000` / 6,224,480 bytes.
The image matches PlatformIO's generated `firmware.factory.bin` over its complete
length; the remainder of the first 16 MB is verified `0xFF` padding. This makes the
factory-to-CrossMux initialization scope explicit, including clearing covered
NVS/app/storage state. It does not target the microSD, eFuses or Flash above 16 MB.
The file has not been publicly published. The user approved it and it was written to this unit; see installation evidence below.

A concrete first-install/recovery plan was supplied to the user and approval for
that exact first-16-MB factory overwrite requested. Approval was explicitly granted and the write completed; the earlier pending state is superseded. The verified factory backup remains user-managed. The CH343 automatic
ROM-entry path is proven by read-only operations. For a full raw factory dump,
recovery is an esptool write at address 0x0 using the user's backup filename; a
packaged backup must use its own verified restore procedure rather than being
mistaken for a raw dump. No repeat backup or disclosure of its path is required.

Startup inspection: `src/main.cpp:869–875` has a bounded 250 ms development delay,
not an indefinite native-USB wait; `lib/Logging/Logging.cpp:66–67` uses ROM/UART
output for Sticky. Actual boot logs and display/input observations remain pending.

### Authorized installation — 2026-11-05

User explicitly approved the exact first-16-MB installation plan in chat. Existing verified factory backup remains user-managed. Prewrite image SHA-256, bridge serial and MCU MAC matched; proceeding with the approved immutable image. Flash and physical results will be recorded separately.


### Installed image and verification reconciliation — 2026-11-05

The user explicitly approved the exact prepared image and first-16-MB replacement.
Prewrite bridge serial, MCU MAC, image length and SHA-256 matched the approved
plan. Esptool 5.6.0 at 460800 baud wrote 16,777,216 bytes at 0x0 in 118.8 seconds,
reported **Hash of data verified**, exited 0 and reset the unit. The recorded
write range was exactly 0x00000000–0x00FFFFFF. No second backup was requested.
Log: `sticky-flash-write.log`; authorization/write status: `sticky-flash-state.json`.

The planned separate **post-boot whole padded-image comparison failed** with a
digest mismatch (`sticky-flash-verify.log`, exit 2). This was investigated without
reflashing or changing source. The approved image contains blank NVS, which the
firmware can initialize on boot; Arduino core 3.3.11 calls `nvs_flash_init()` in
`cores/esp32/esp32-hal-misc.c:331`.

Read-only isolation established:

- **passed:** 0x00000000–0x00008FFF matched the approved image exactly.
- **passed:** 0x00010000–0x00FFFFFF matched the approved image exactly, including
  the application, inactive app/storage regions and padding. Esptool digest checks
  for both ranges exited 0 (`sticky-immutable-verify.log`).
- Read only the remaining NVS/OTA span (0x9000, 0x7000 bytes). Offline comparison
  found **128 changed bytes in NVS**, first 0x9000, last 0x90BF; **zero** changed
  bytes in otadata. Combined with the two matching ranges, the whole-image
  mismatch is localized to initialized NVS, not firmware corruption.
- Raw NVS capture stays private under the local tools directory. Only byte counts
  and offsets are recorded here (`flash-check/mutable-diff-summary.json`).

For subsequent verification, compare immutable ranges and classify mutable NVS /
OTA state separately. Do not expect a padded full-install file to remain a
byte-for-byte representation of an initialized running device. Preserve the failed
whole-image comparison as evidence; no reflash was necessary to resolve it.

A 40-second controlled boot capture (`sticky-first-boot.log`) identified
`Hardware detect: sticky`, SDK IMU initialization and an external RTC. It then
logged `Power-button wake not held through verification, sleeping`. No home-screen,
SD-reading, touch, memory or display-quality acceptance follows from this limited
boot. The user was asked to hold power about two seconds, release it, and report
the actual screen/orientation/readability. A bounded passive UART capture was
started for that normal wake; no deliberate reset during the user's test.

**Pending:** user screen observation, normal boot/reading/input, persistence,
USB/battery/charge and three sleep/wake cycles, heap/PSRAM and current measurements.
Draft PR #458 must remain Draft. SDK/source runtime code remains unchanged.

The 90-second passive wake capture ended with zero UART bytes. No user screen observation was available at this checkpoint; normal wake remains pending, not failed or passed. The serial port was closed after the bounded capture.
