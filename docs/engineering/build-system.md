# Build System & Build Flags

> Deep reference for [CLAUDE.md](../../CLAUDE.md). Covers PlatformIO usage, the
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
  * `papermono`: M5Stack PaperMono ESP32-S3 development build
  * `eego_a4`: eego A4 ESP32-S3 experimental development build
  * `murphy_m4`: Murphy M4 ESP32-S3 experimental development build
  * `waveshare_epaper_397`: Waveshare ePaper 3.97 ESP32-S3 experimental development build
  * `simulator`: Native X4 desktop simulator supplied by the pinned simulator fork
  * `simulator_x3`: Native X3 desktop simulator
  * `simulator_eego_a4`: Native 768x552 eego A4 product simulator
  * `simulator_murphy_m4`: Native 800x480 Murphy M4 product simulator

The six S3 environments are separate hardware binaries, but each is a unified
language firmware. `bin/ci-check` builds them together with the default C3 target.

Routine pull-request CI builds only `default` and `x4pro`. `default` remains the
shared X3/X4 firmware with runtime device detection. The path-filtered Hardware
CI workflow builds all four simulators and all six S3 environments when
hardware-sensitive files change, and can also be started manually.

## Desktop Simulator

Install SDL2 and `curl` (plus OpenSSL development headers on Linux), place EPUB
files under `fs_/books/`, and run:

```bash
pio run -e simulator -t run_simulator
pio run -e simulator_x3 -t run_simulator
pio run -e simulator_eego_a4 -t run_simulator
pio run -e simulator_murphy_m4 -t run_simulator
```

The simulator implementation and launcher come from the pinned
[`0x1abin/crosspoint-simulator`](https://github.com/0x1abin/crosspoint-simulator/tree/6058c3da013fbe1579d41c7c5cc77cd466d37f12)
fork.
The firmware repository does not carry a second host implementation. Arrow
keys are Up/Down, `P` is Power,
mouse input provides touch, and `S` sleeps. A4 additionally maps `H` to a short
Back or one-shot Home after 700 ms; M4 ignores `H`. Once A4/M4 is asleep, only
Power wakes it.

This product-level simulator covers UI, input, RTC state, M4 frontlight state,
and sleep/wake flows. It does not emulate EPD waveforms or ghosting, bus timing,
SDMMC contention, PSRAM, or power consumption.

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
