# Build System & Build Flags

> Deep reference for [AGENTS.md](../../AGENTS.md). Covers PlatformIO usage, the
> build environments, the critical build flags that change firmware behavior, and
> personal local overrides.

## Build System: PlatformIO

**PlatformIO is BOTH a VS Code extension AND a CLI tool**:

1. **VS Code Extension** (Recommended):
   * Extension ID: `platformio.platformio-ide` (see `.vscode/extensions.json`)
   * Provides: Toolbar buttons, IntelliSense, integrated build/upload/monitor
   * Configuration: `.vscode/c_cpp_properties.json`, `.vscode/tasks.json`
   * Usage: Click Build (✓), Upload (→), or Monitor (🔌) buttons

2. **CLI Tool** (`pio` command):
   * **Installation**: Python package (typically `pip install platformio`)
   * **Windows Location**: `C:\Users\<user>\AppData\Local\Programs\Python\Python3xx\Scripts\pio.exe`
   * **Verify**: `which pio` (Git Bash) or `where.exe pio` (cmd)
   * **Usage**: `pio run`, `pio run -t upload`, etc.

**Configuration Files**:
* `platformio.ini`: Main build configuration (committed to git)
* `platformio.local.ini`: Local overrides (gitignored, create if needed)
* `partitions.csv`: ESP32 flash partition layout

## Tool initialization

Use pioarduino 6.2.0 with the pinned ESP32 platform. Before building with a new
PlatformIO tool directory, run from the repository root:

```bash
python -m pip install pioarduino==6.2.0
python scripts/patch_pioarduino_cache.py --prepare-platform
```

The platform's IDF installer otherwise replaces the Core's running SCons
package, causing missing Python modules during a clean build. Preparation
removes SCons from that installer's tool list; Core still supplies it. It does
not change compiler optimization flags. CI performs the same preparation.
Nightly and CI use a separate `PLATFORMIO_CORE_DIR` per build job so concurrent
runner jobs cannot change each other's installed toolchains. The directory name
includes `.platformio`: pioarduino's framework-restoration path check requires
that component when switching from a custom SDK back to prebuilt TinyUSB.
On Ubuntu, the bundled cppcheck executable also requires `libpcre3`.

The custom SDK bootstrap uses non-LTO objects; the final application retains
its existing LTO configuration. Keep the verified C3 optimization options and
partition layout unchanged; sufficient Flash headroom is not a reason to tune
additional compiler options.

X4 Pro, X4 Classic, Murphy M4, Waveshare 3.97 and PaperMono use application LTO (`-flto=auto`, removing the packaged `-fno-lto`)
to keep the complete reader inside the existing 6.25 MiB OTA partitions. Their
prebuilt `dio_opi` core continues to supply the TinyUSB CDC/MSC component graph.

## Build Environment
* **Standard**: C++20 (`-std=c++2a`). No Exceptions, No RTTI.
* **Logging**: ALWAYS use `LOG_INF`, `LOG_DBG`, or `LOG_ERR` from `Logging.h`. Raw Serial output is deprecated.
* **Environments** (in `platformio.ini`):
  * `default`: Development (LOG_LEVEL=2, serial enabled)
  * `gh_release`: Production (LOG_LEVEL=0)
  * `gh_release_rc`: Release candidate (LOG_LEVEL=1)
  * `slim`: Minimal build (no serial logging)
  * `sticky`: Seeed Sticky ESP32-S3 development build
  * `x4pro`: Xteink X4 Pro ESP32-S3 development build
  * `x4c`: Xteink X4 Classic ESP32-S3 build-only development build
  * `papermono`: M5Stack PaperMono ESP32-S3 development build
  * `eego_a4`: eego A4 ESP32-S3 experimental development build
  * `murphy_m4`: Murphy M4 ESP32-S3 experimental development build
  * `waveshare_epaper_397`: Waveshare ePaper 3.97 ESP32-S3 experimental development build
  * `simulator`: Native X4 desktop simulator supplied by the pinned simulator fork
  * `simulator_x3`: Native X3 desktop simulator
  * `simulator_eego_a4`: Native 768x552 eego A4 product simulator
  * `simulator_murphy_m4`: Native 800x480 Murphy M4 product simulator
  * `simulator_readpico`: Native 1216x684 Read Pico simulator, 684x1216 in portrait

The seven S3 environments are separate hardware binaries, but each is a unified
language firmware. `bin/ci-check` builds the default C3 target and six S3 release
targets; X4 Classic is build-only and covered separately by Hardware CI.

Routine pull-request CI builds only `default` and `x4pro`. `default` remains the
shared X3/X4 firmware with runtime device detection. The path-filtered Hardware
CI workflow builds all five simulators and the configured S3 environments when
hardware-sensitive files change, and can also be started manually.

Bluetooth Page Turner Beta is compiled into every hardware environment,
including development, Nightly, release-candidate, stable, and slim builds.
The runtime Bluetooth switch defaults to off; native simulators use SDK stubs.
C3 environments inherit the internal-RAM and Flash-controller configuration
from `c3_hardware`. S3 hardware profiles inherit the PSRAM and IPC configuration.
Sticky and eego A4 use the custom-core controller-only NimBLE configuration;
the other five S3 targets retain their prebuilt `dio_opi` core so the TinyUSB
MSC component graph remains intact. See [C3 Bluetooth](c3-bluetooth.md) for
memory gates, validation results, and remaining hardware acceptance work.

The SDK's obsolete passkey callback is removed only from a generated source copy
under `$BUILD_DIR/ble-compat`; the SDK and NimBLE dependency sources are never
rewritten. The source is a build dependency and unexpected callback signatures
fail the build. The same translation unit includes the `_btLibraryInUse` weak
shim for both the custom-core bootstrap (which omits application sources) and
the final firmware, without suppressing NimBLE's Arduino BT usage header.

The pinned prebuilt S3 core still creates 1 KiB IPC stacks. BLE controller
interrupt allocation can overflow `ipc0`; upstream Arduino lib-builder #386
raises the budget to 2 KiB. S3 BLE builds use a narrow link adapter at task
creation to apply that minimum only to `ipc0`/`ipc1` on their matching cores.
It adds at most 2 KiB of internal stack RAM across both tasks and preserves
larger configured stacks, allocation failures, other tasks, and the prebuilt
TinyUSB core. The adapter travels with the same bootstrap-compatible source;
remove it when the pinned core supplies the upstream budget. BLE diagnostics
include both IPC stack high-water marks; check them after repeated starts.

When switching from a custom core to a prebuilt target, retain the framework's
`sdkconfig.orig` marker until PlatformIO restores the original core package.
Restoring only `sdkconfig` leaves custom IDF archives behind; mixing these with
an untouched `dio_opi` header can omit PSRAM initialization entirely.
`scripts/tests/test_pioarduino_cache.py` covers this transition.

For isolated cache-switch validation, run builds sequentially with all four
overrides below (the directories are gitignored). Do not copy compiled core
packages from an existing PlatformIO installation into this environment.

```bash
export PLATFORMIO_CORE_DIR="$PWD/.platformio/ble-psram"
export PLATFORMIO_BUILD_DIR="$PWD/.pio/ble-psram-build"
export PLATFORMIO_BUILD_CACHE_DIR="$PWD/.cache/ble-psram"
export IDF_COMPONENT_CACHE_PATH="$PWD/.cache/ble-psram-idf-components"
pio run -e sticky_nightly
pio run -e waveshare_epaper_397_nightly
pio run -e eego_a4_nightly
pio run -e waveshare_epaper_397_nightly
```

Use the same overrides when uploading. Check the resulting ELF for the actual
PSRAM initialization and heap-registration call paths, not just the
`BOARD_HAS_PSRAM` macro or `psramInit` symbol. Runtime BLE diagnostics must report
nonzero PSRAM capacity and a successful allocator probe before connection tests.

## Desktop Simulator

Install SDL2 and `curl` (plus OpenSSL development headers on Linux), place EPUB
files under `fs_/books/`, and run:

```bash
pio run -e simulator -t run_simulator
pio run -e simulator_x3 -t run_simulator
pio run -e simulator_eego_a4 -t run_simulator
pio run -e simulator_murphy_m4 -t run_simulator
pio run -e simulator_readpico -t run_simulator
```

The simulator implementation and launcher come from the pinned
[`0x1abin/crosspoint-simulator`](https://github.com/0x1abin/crosspoint-simulator)
fork; the exact revision is recorded in `platformio.ini`.
The firmware repository does not carry a second host implementation. Arrow
keys are Up/Down, `P` is Power,
mouse input provides touch, and `S` sleeps. A4 additionally maps `H` to a short
Back or one-shot Home after 700 ms; M4 ignores `H`. Once A4/M4 is asleep, only
Power wakes it.

This product-level simulator covers UI, input, RTC state, M4 frontlight state,
and sleep/wake flows. It does not emulate EPD waveforms or ghosting, bus timing,
SDMMC contention, PSRAM, or power consumption.

Read Pico uses its 103,968-byte B/W framebuffer, sixteen-level image transactions,
and the same 12/12/14 pt SD UI font selection as the hardware. The window fits the
usable desktop area without upscaling; screenshots retain the logical panel size
and current rotation, independent of the window's scale or HiDPI density. Existing
device window and screenshot behavior is retained. Read Pico has no frontlight or
Home key; Up/Escape/Down represent its capacitive strip, `P` is Power, and only
Power wakes it. See [the device guide](read-pico.md#desktop-simulator).

## Critical Build Flags
These flags in `platformio.ini` fundamentally affect firmware behavior:

```cpp
-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1  // Single framebuffer (saves 48KB RAM!)
-DARDUINO_USB_MODE=1                 // Enable USB CDC
-DARDUINO_USB_CDC_ON_BOOT=1          // Serial available immediately at boot
-DXML_CONTEXT_BYTES=1024             // XML parser memory limit (EPUB parsing)
-DUSE_UTF8_LONG_NAMES=1              // SD card long filename support
-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1   // Avoid zlib name conflicts
-DXML_GE=0                           // Disable XML general entities (security)
-DDESTRUCTOR_CLOSES_FILE=1           // FsFile destructor auto-closes (SdFat)
```

**DESTRUCTOR_CLOSES_FILE implications**:
- SdFat's `FsBaseFile` destructor calls `close()` automatically when the object goes out of scope
- **Do NOT add explicit `file.close()` calls** for local `FsFile` variables — the destructor handles it
- Explicit `close()` is still required in these cases:
  1. **Close before delete**: Must close before `Storage.remove()` on the same path
  2. **Close before reopen**: Must close before reopening the same `FsFile` variable (e.g., write then reopen for read, or rewrite the same path)
  3. **Member variables**: `FsFile` members persist beyond any single function scope, so close at the intended release point (e.g., in `onExit()`)

**SINGLE_BUFFER_MODE implications**:
- Only ONE framebuffer exists (not double-buffered)
- Grayscale rendering requires temporary buffer allocation (`renderer.storeBwBuffer()`)
- Must call `renderer.restoreBwBuffer()` to free temporary buffers
- See [lib/GfxRenderer/GfxRenderer.cpp:439-440](../../lib/GfxRenderer/GfxRenderer.cpp) for malloc usage

**X4 SSD1677 display implications**:
- The application does not override display-driver configuration. The SDK's
  active X4 board profile selects the SSD1677 and its in-spec 20 MHz SPI clock.
- Refresh waveforms come from the SDK's active board config. X4 FAST refreshes
  use the stock absolute sequence (`0xFC`), which includes the temperature and
  power sequencing needed to avoid the persistent ghosting seen with the
  weaker incremental `0x1C` path.
- X3 is runtime-selected before display initialization and uses its UC81xx
  driver and SPI configuration unchanged. X4 Pro probes its SSD1677/UC81xx
  controller once before display initialization. Sticky retains its
  board-specific SSD1677 waveform config.

---

## Local Development Configuration

### platformio.local.ini (Personal Overrides)

**Purpose**: Personal development settings that should NEVER be committed.

**Use Cases**:
- Serial port configuration (varies by machine)
- Debug flags for specific testing
- Local build optimizations
- Developer-specific paths

**Example** `platformio.local.ini`:
```ini
# platformio.local.ini (gitignored)
[env:default]
upload_port = COM7              # Windows: COMx, Linux: /dev/ttyUSBx
monitor_port = COM7

build_flags =
  ${base.build_flags}
  -DMY_DEBUG_FLAG=1             # Personal debug flags
  -DTEST_FEATURE_ENABLED=1
```

**Configuration Hierarchy**:
1. `platformio.ini` - **Committed**, shared project settings
2. `platformio.local.ini` - **Gitignored**, personal overrides
3. Local file extends/overrides base config

**Rules**:
- **NEVER commit** `platformio.local.ini`
- **NEVER put** personal info (serial ports, credentials) in main `platformio.ini`
- Use `${base.build_flags}` to extend (not replace) base flags

See also: [getting-started](../contributing/getting-started.md) for first-time toolchain setup, [testing-and-debugging.md](testing-and-debugging.md) for build/monitor commands.

### Fixed local integration builds (2026-10-02)

The SDK/Simulator/Reader pins are `98b4e427`, `20e73803`, and `38280863`.
Use the real source exports recorded by `sync-upstream start --local-rehearsal`;
record their complete Git tree fingerprints with validation artifacts. The SDK
fork includes all six ReadPico fixes through `e3550ec`. Production dependency
commits remain unchanged during rehearsal; the ignored local PlatformIO config
selects reviewed exports. No commit, push, PR or flashing is part of this stage.

Validate `default`, `gh_release`, `readpico`, `readpico_nightly`, and `metalio_eink4`,
and all six existing simulator environments. Only the explicit ReadPico profiles
use high-density metrics. Keep host tests, whole-page visuals, Flash/static RAM,
and physical-device acceptance as separate results.

The integrated SD catalog retains at most 48KiB of row/container data and checks
32KiB free-heap plus 4KiB contiguous headroom before growth. Installed discovery
is bounded to 32 entries, reuses root lookup instead of keeping every seen name,
and rejects excess picker/history growth with a log. Manifest/title/description
inputs are bounded; JSON rows accept 768-byte fields, 64 bundle files and 4KiB
total captured text, with 32 nesting/path segments and 128-byte keys. Browse
responses remain on SD (up to 1MiB), while API responses retain the upstream
48KiB cap and fallible Arduino String reserve. TLS is released before downloads
and on exit. These limits protect C3 shared code; simulator heap numbers are
synthetic and do not verify hardware runtime headroom.
