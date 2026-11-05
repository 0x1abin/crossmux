# reTerminal Sticky V1 bring-up

Updated 2026-11-05 (America/Los_Angeles). This record follows one V1 unit against
the existing Sticky support. Historical tests on other units do not establish
acceptance of this source revision on this unit.

## Current state

Reuse the existing SDK profile and CrossMux `sticky` environment. No driver/HAL
change is justified by current evidence. The focused display host suite passed;
the firmware build is pending after environment fixes. Factory firmware remains
installed. No Flash erase/write, commit, push or PR has occurred yet.

## Hardware contract and evidence

| Area | Evidence and unresolved facts |
|---|---|
| Unit | User confirms reTerminal Sticky **V1**, factory firmware, Windows 11 USB connection and installed microSD |
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
| Firmware version setting | `2.0.0`; built-image version/hash pending |
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
| Firmware build | `sticky`, source above | pending | Retry running; prior environment failures retained in `sticky-build-attempt1-encoding.log` / `sticky-build-attempt2-component-cache.log` |
| Code-change regressions / `./bin/ci-check` | Current documentation-only diff | not applicable | Repository workflow exempts documentation-only changes; reassess if code changes are needed |
| Documentation whitespace/local links | Current diff | pending | Required before commit |
| Factory backup available | Actual unit | passed | User explicitly confirmed verified backup; path remains user-managed |
| Image/offset/recovery plan and overwrite authorization | Actual unit | pending | Requires a successful build and a concrete reviewed migration |
| Flash and hash verification | Actual unit | pending | No write performed |
| Boot/display/orientation/touch/buttons | Actual unit and final image | pending | Real logs/visual/button observations needed |
| SD/EPUB/progress/settings, missing-SD behavior | Actual unit and final image | pending | Copy fixture; test reading and persistence |
| USB/battery/charge/shutdown, at least 3 sleep/wake cycles | Actual unit and final image | pending | Real observations and post-wake peripheral checks |
| Internal heap/minimum/largest block and PSRAM | Actual unit | pending | Capture before/after init and repeated reading/sleep; investigate continuing decline |
| Operating/standby current | Actual unit | pending | No measurement or power-saving claim |
| Optional peripherals | Not separately requested | not applicable | No optional-peripheral acceptance claimed |
| SDK contribution | No SDK changes | not applicable | Existing gitlink retained |
| Publication | Personal fork ready | pending | No commit/push/PR yet; Draft until task validation requirements pass |

## Contribution and next actions

GitHub CLI 2.103.3 authenticated as `neilnorthrop`. Personal fork
`https://github.com/neilnorthrop/crossmux` is remote `fork`; `origin` remains the
CrossMux maintainer repository. No maintainer coordination has occurred or been
claimed. The current PR template's stock-firmware comparison remains unverified;
document the existing port and this unit's evidence without claiming new support.
AI use must be disclosed. Do not merge or publish firmware.

Next: complete the baseline build; inspect the generated image/partitions and
Flash headroom; provide a concrete migration/recovery plan before requesting
overwrite approval. Then flash only within that authorization and collect real
physical results. Keep the PR Draft for any pending/failed validation or SDK
dependency. Resume from this record and the actual worktree, not historical
claims in other device documents.
