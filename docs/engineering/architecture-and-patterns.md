# Architecture & Common Patterns

> Deep reference for [AGENTS.md](../../AGENTS.md). For the big-picture runtime
> lifecycle, activity model, and dataflow diagrams, see
> [../contributing/architecture.md](../contributing/architecture.md). This file
> covers the agent-facing details: directory layout, the HAL, and the reusable
> code patterns (singletons, activity lifecycle, FreeRTOS tasks, fonts).

## Directory Structure
* lib/: Internal libraries (Epub engine, GfxRenderer, UITheme, I18n)
  * lib/hal/: Hardware Abstraction Layer (HalDisplay, HalGPIO, HalStorage)
  * lib/I18n/: Internationalization (translations in `translations/*.yaml`, generated string tables)
* src/activities/: UI logic using the Activity Lifecycle (onEnter, loop, onExit)
* freeink-sdk/: Low-level SDK (FreeInkDisplay, InputManager, BatteryMonitor, SDCardManager)
* .crosspoint/: SD-based binary cache for EPUB metadata and pre-rendered layout sections

## Hardware Abstraction Layer (HAL)

**CRITICAL**: Always use HAL classes, NOT SDK classes directly.

| HAL Class | Wraps SDK Class | Purpose | Singleton Macro |
|-----------|----------------|---------|-----------------|
| `HalDisplay` | `EInkDisplay` | E-ink display control | *(none)* |
| `HalGPIO` | `InputManager` | Button input handling | *(none)* |
| `HalStorage` | `SDCardManager` | SD card file I/O | `Storage` |

**Location**: [lib/hal/](../../lib/hal/)

**Why HAL?**
- Provides consistent error logging per module
- Abstracts SDK implementation details
- Centralizes resource management

**Example - HalStorage**:
```cpp
#include <HalStorage.h>

// Use Storage singleton (defined via macro)
HalFile file;
if (Storage.openFileForRead("MODULE", "/path/to/file.bin", file)) {
  // Read from file
  // No file.close() needed — DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}
```

**Usage**: Use `HalFile` (the mutex-wrapping handle), NOT raw SdFat `FsFile` or Arduino `File`. Do NOT add `file.close()` for local variables (see [build-system.md](build-system.md) → DESTRUCTOR_CLOSES_FILE).

**SdFat is not thread-safe; all SD access MUST go through HalStorage**:
- SdFat's `SdSpiCard` tracks SPI bus state with an unsynchronized `m_spiActive` bool. Two tasks calling SdFat concurrently can confuse that state machine and end with one task calling `SPIClass::endTransaction()` against a paramLock the *other* task is holding. That trips FreeRTOS's `xTaskPriorityDisinherit` assert (`tasks.c:5156, pxTCB == pxCurrentTCBs[0]`) and panics the system. See SdFat issue #518.
- `HalStorage` serializes everything via `storageMutex`. Downstream code uses `HalFile` (declared in `<HalStorage.h>`); every method call (read, write, seek, close) takes the mutex. `HalFile`'s destructor also takes the mutex before letting the underlying SdFat `FsFile` close.
- **Never** call into `SdFat` / `SdSpiCard` / `FsBaseFile` / `SDCardManager` / raw `FsFile` directly — that bypasses the mutex.

**CPU frequency changes must go through `HalPowerManager::setPowerSaving()`**:
- `modeMutex` protects the target-mode decision, the entire `setCpuFrequencyMhz()` call, and the `isLowPower` update. Update state only after a successful transition; repeated requests for the current state do nothing.
- Arduino's APB callbacks hold SPI locks between the BEFORE and AFTER callbacks. Concurrent frequency changes can deadlock: one task holds a SPI lock while waiting for the APB callback lock, and another holds the APB callback lock while waiting for that SPI lock. Serializing only the state assignment is insufficient.
- `HalPowerManager::Lock` sets `NormalSpeed` under `modeMutex`, then releases the mutex **before** calling `setPowerSaving(false)`. Keep that ordering: the mutex is not recursive. Obtain the power lock before starting SPI work. The existing helper supports only one active power lock; it is not a nested-lock counter.
- Preserve the Wi-Fi/C3 BLE host guards and board-specific frequency restrictions. Light sleep and CPU downclocking are separate decisions; fixing clock serialization must not disable either Bluetooth or standby light sleep.

`Storage.getSpace(totalBytes, freeBytes)` queries filesystem data capacity, not
raw card size, through the SDK's unified capacity query under the same mutex.
Both outputs are zero on failure; callers must check the boolean result before
using them. The SDK derives cluster bytes from 512-byte sectors with 64-bit
arithmetic because SdFat 2.3.1's FAT `bytesPerCluster()` truncates 64 KiB to zero.
Successful free-space scans are cached for 20 seconds from completion; failures
invalidate the cache and are retried on the next query. Mount/unmount resets it.
The cache is a snapshot, not an immediate accounting of subsequent writes.
The About page paints its loading state before this synchronous query; input
waits for the scan to finish. Host simulator capacity comes from `statvfs`, so
use `python3 scripts/tests/test_sd_space.py` for FAT calculation regressions.

---

## Common Patterns

### Singleton Access
**Available Singletons**:
```cpp
#define SETTINGS CrossPointSettings::getInstance()  // User settings
#define APP_STATE CrossPointState::getInstance()    // Runtime state
#define GUI UITheme::getInstance()                   // Current theme
#define Storage HalStorage::getInstance()            // SD card I/O
#define I18N I18n::getInstance()                     // Internationalization
```

### Activity Lifecycle and Memory Management

`ActivityManager` owns heap-allocated Activities through `std::unique_ptr`.
Replace/pop calls `onExit()` before destruction; push retains the parent and
includes it in the peak-memory budget. Create transitions through the manager's
checked allocation helpers, not raw `new`/`delete`.

`onExit()` runs while `RenderLock` is held; `onEnter()` runs after releasing it.
Never acquire a second render lock or call `requestUpdateAndWait()` while holding
that lock. Stop/delete workers before destruction, release `onEnter()` resources
in `onExit()`, and close member `HalFile` handles at their intended release point.
Normal Activities read the mapped-input snapshot updated once by the main loop.

### Read Pico idle page cache

The EPUB reader prepares the next text page on the existing render task, once
400 ms have elapsed after a foreground render without another render notification.
Other activities return zero from `idleRenderDelayMs()` and add no idle wakeups.
Input, foreground render requests and activity switches cancel the idle pass;
its timeout must not acknowledge a `requestUpdateAndWait()` caller.

The cache uses four reusable PSRAM frames (415,872 bytes on Read Pico), released
in `onExit()`. A scoped restore protects the live framebuffer and render mode.
Glyphs load on demand, with cancellation between page elements and planes;
a single SD read or glyph render is not preemptible. Cache hits compare the
resolved layout, drawing settings, chapter generation and render epoch. Image
pages, failed loads and cancelled attempts do not retry until another foreground
render. Allocation failure disables the cache for the reading session.

Run `python3 -m unittest discover -s scripts/tests -p test_reader_page_cache.py -v`
for cache identity, cancellation, allocation cleanup and render-task scheduling.
Device acceptance must still check short taps/swipes/menu entry during cache
construction, reflow to the old cached page number, image-page idle SD activity,
framebuffer integrity and PSRAM recovery after leaving/reopening the reader.

### FreeRTOS Task Guidelines

`ActivityManager::begin()` owns the shared render task; avoid per-screen render
tasks. ESP-IDF task stack sizes are bytes. Size workers from their call chains
and measured `uxTaskGetStackHighWaterMark()`, and inspect the active loop/render
stack rather than copying a generic example. Stop workers in `onExit()` before
destruction and synchronize shared state. Network flows release WiFi/power locks
on exit; trace `HalPowerManager` before changing network or sleep lifetime.

### Global Font Loading

**Source**: [src/main.cpp:40-115](../../src/main.cpp)

**Built-in fonts are global static objects** at firmware startup:
- International UI fonts: 8, 10, 12pt
- Simplified-Chinese UI fallbacks: 8, 10, 12pt
- Offline reader fallback: 12pt; other sizes and styles use SD `.cpfont`

OpenDyslexic and the broad Noto reader families live in the SD font catalog
(`lib/EpdFont/scripts/sd-fonts.yaml`). Legacy IDs resolve to the 12pt fallback.

**Compilation Flag**:
```cpp
#ifndef OMIT_FONTS
  // Most fonts loaded here
#endif
```

**Implications**:
- Fonts stored in **Flash** (marked as `static const` in `lib/EpdFont/builtinFonts/`)
- Font rendering data cached in **DRAM** when first used
- `OMIT_FONTS` can reduce binary size for minimal builds
- Font IDs defined in [src/fontIds.h](../../src/fontIds.h)

**Usage**:
```cpp
#include "fontIds.h"

renderer.insertFont(FONT_UI_MEDIUM, ui12FontFamily);
renderer.drawText(FONT_UI_MEDIUM, x, y, "Hello", true);
```

## Framebuffer and storage ownership

The C3 baseline owns one runtime-panel-sized framebuffer. Temporary grayscale
backups are chunked; do not add a second full-screen allocation. Pair successful
`storeBwBuffer()` with restore or discard when the underlying page changed.
`restoreBwBuffer(false)` preserves the newer panel differential baseline after
an overlay. `FrameBufferLoan` forbids drawing/display while loaned; release
exclusive `BuildScratch` claims before restoration and redraw afterward.

USB Drive transfers exclusive raw SD ownership to the host. Stop filesystem
users and pending navigation through `UsbDriveActivity` and
`ActivityManager::loop()`. Exit by the required restart rather than reopening
filesystem access early.

## Runtime fonts and services

Built-in glyph data is static; decompression/render caches consume runtime RAM.
`.cpfont` works across targets; direct TTF/OTF/TTC requires the vector-font flag
and usable PSRAM. Follow [SD-font contracts](../sd-card-fonts.md) for discovery,
precedence, styles, sizes and whole-string UI fallback. Use
`FontCacheManager::PrewarmScope` around scan/prewarm/render, keep font/style caches
distinct, and respect kerning/ligature view lifetimes. `clearCache()` keeps SD
mini arenas/advances; `releaseSdFontCaches()` releases rebuildable caches at the
established heap-critical transitions while keeping fonts loaded.

Firmware executes declarative plugin manifests/request templates; `plugin.js`
runs in the browser. Preserve [plugin isolation and jobs](../sd-plugins.md),
[event/outbox delivery](../plugin-events.md), and
[HTTP contracts](../webserver-endpoints.md). Normalize external paths and escape
filenames through existing helpers. Protected reads follow `Epub` and the pinned
SDK `ContentProtection`; keep service activation policy in plugins.

Reader grayscale submission uses the HAL's existing SDK `displayCommitted()`
contract. Whole-plane reading saves B/W pixels before deferring a base; one
private execution path restores BW mode and releases that snapshot on success,
failed commit or cancellation. Snapshot OOM falls back to an ordinary B/W
submission. Refresh cadence, render statistics and saved progress advance only
for committed pages. Manual sync cannot persist a requested page that has not
committed; returning from a footnote retains the previously visited origin.
ReadPico supports Overlay selectors and Native16, without advertising
Absolute/Direct or strip uploads. Unsupported controllers reject legacy gray
planes/LUT calls and can display the B/W fallback.
