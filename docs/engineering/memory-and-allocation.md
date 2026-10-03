# Memory Safety & Heap Allocation

> Deep reference for [AGENTS.md](../../AGENTS.md). On ESP32-C3 with
> `-fno-exceptions`, a failed bare `new` calls `abort()` — allocation discipline
> is a stability requirement, not a style preference.

## Memory Safety and RAII
* Smart Pointers: Prefer std::unique_ptr. Avoid std::shared_ptr (unnecessary atomic overhead for a single-core RISC-V).
* RAII: Use destructors for cleanup. Call `vTaskDelete()` explicitly for deterministic task release. Do NOT call `file.close()` on local `FsFile` variables — `DESTRUCTOR_CLOSES_FILE=1` handles it at scope exit (see [build-system.md](build-system.md) → Critical Build Flags).

> For the general error-handling pattern hierarchy (LOG_ERR + return false,
> fallback, assert, ESP.restart), see
> [coding-standards.md](coding-standards.md) → Error Handling Philosophy.

---

## Heap Buffer Allocation

**Prefer `makeUniqueNoThrow` over `malloc`.** Both are nothrow (return `nullptr` on OOM rather than calling `abort()`), but `malloc` requires a manual `free` on every return path — a common source of leaks. `makeUniqueNoThrow<uint8_t[]>(size)` from `lib/Memory/Memory.h` frees automatically when it goes out of scope.

**Preferred pattern**:
```cpp
#include <Memory.h>

auto buffer = makeUniqueNoThrow<uint8_t[]>(bufferSize);
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}

processData(buffer.get(), bufferSize);
// freed automatically — no manual free needed, no leak on early return
```

**`malloc` or `new (std::nothrow)` are still acceptable** when the buffer must be passed to a C API that takes ownership and frees it itself (e.g., certain SDK callbacks). In that case follow the manual pattern:
```cpp
auto* buffer = static_cast<uint8_t*>(malloc(bufferSize));  // or new (std::nothrow) uint8_t[bufferSize]
if (!buffer) {
  LOG_ERR("MODULE", "OOM: %d bytes", bufferSize);
  return false;
}
sdkApiThatTakesOwnership(buffer, bufferSize);  // SDK calls free() / delete[]
```

**Rules**:
- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **ALWAYS check for nullptr** after any allocation and `LOG_ERR` before returning false
- **Raw allocation only** when a C API takes ownership; document why in a comment

**Examples in codebase**:
- Memory utilities: [Memory.h](../../lib/Memory/Memory.h) (`makeUniqueNoThrow`)
- Activity transitions: [Activity.h](../../src/activities/Activity.h) and
  [ActivityManager.h](../../src/activities/ActivityManager.h)
- Bitmap rendering scratch: [Bitmap.h](../../lib/GfxRenderer/Bitmap.h)

## Heap Allocation with `new`: Always Use `makeUniqueNoThrow`

**CRITICAL**: With `-fno-exceptions`, bare `new` on OOM calls `abort()` — it does NOT return `nullptr`. Always use `makeUniqueNoThrow` from `lib/Memory/Memory.h`, which wraps `new (std::nothrow)` and returns a `std::unique_ptr` that is null on OOM and automatically frees on scope exit.

**Preferred pattern**:
```cpp
#include <Memory.h>

auto obj = makeUniqueNoThrow<MyClass>(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }

auto buf = makeUniqueNoThrow<uint8_t[]>(size);
if (!buf) { LOG_ERR("MOD", "OOM: %d bytes", size); return false; }

// Pass to C APIs via .get(); unique_ptr frees automatically on return
someApi(buf.get(), size);
```

**`new (std::nothrow)` directly is acceptable** when the object must be passed to a C API that takes ownership and calls `delete` itself:
```cpp
auto* obj = new (std::nothrow) MyClass(args);
if (!obj) { LOG_ERR("MOD", "OOM: MyClass"); return false; }
sdkApiThatTakesOwnership(obj);  // SDK calls delete
```

**Rules**:
- **Prefer `makeUniqueNoThrow`** — automatic cleanup eliminates leak risk on error paths
- **NEVER use bare `new`** — always `makeUniqueNoThrow` or `new (std::nothrow)`
- **ALWAYS `LOG_ERR` before returning false** on OOM
- **Use `.get()`** to pass the raw pointer to C-style APIs; ownership stays with the `unique_ptr`
- **`new (std::nothrow)` directly only** when a C API takes ownership; document why in a comment

**Examples in codebase**:
- Memory utilities: [Memory.h](../../lib/Memory/Memory.h) (`makeUniqueNoThrow`)

## Shared Allocation Paths

- Create Activities through `startActivityForResultWith<T>()` or
  `replaceActivityWith<T>()`. Both use the project nothrow allocation path and
  report failure without publishing a partially constructed transition.
- A `Bitmap` owns one draw scratch block. Rendering operations grow that block
  only when necessary and reuse it for source rows, output rows, and alpha
  data; do not add per-draw row allocations in `GfxRenderer`.
- `FrameBufferLoan` temporarily makes the single 48KB framebuffer unavailable
  for drawing and publishes it through `BuildScratch`. Only cold, full-redraw
  paths may hold a loan. `BuildScratch` is exclusive: consumers must tolerate a
  failed `claim()` and release a successful claim before the loan ends.

These mechanisms make large and high-frequency allocations recoverable. They
do not make the firmware globally OOM-safe: ordinary `std::string` and
`std::vector` growth still uses throwing allocation, and exceptions are
disabled. Bound external lengths, reserve before append loops, and avoid
unbounded container growth on device-controlled input.

## Optional PSRAM

ESP32-C3 remains the no-PSRAM baseline. On targets declaring
`BOARD_HAS_PSRAM`, use `memory::makePsramByteBufferNoThrow()` only for large,
sequential buffers whose main benefit is avoiding storage I/O or preserving an
asynchronous render path. Keep the framebuffer, decoder state, current-page
font mini-cache, small scratch buffers, and other frequently accessed data in
internal DRAM. A bounded cache of immutable font source bytes may use PSRAM as
long as glyphs are copied back into the internal mini-cache before rendering.

Prefer internal DRAM when its free-size and largest-block reserves are healthy,
then try PSRAM, and finally retain the existing low-memory business fallback.
`memory::psramHasHeadroom()` reserves 256 KB of PSRAM and returns false on the
simulator, devices without the capability macro, failed PSRAM initialization,
or insufficient contiguous space. `memory::ByteBuffer` owns both internal and
PSRAM heap-cap allocations through RAII, so every early return releases them.

The Inx recent-books screen applies this policy to complete, validated 1-bit
thumbnail BMP files: it reads each file sequentially into PSRAM once, then the
normal `Bitmap` row scratch copies rows back into internal DRAM for hot pixel
processing. The cache is bounded to 64 KB per file and 512 KB per Activity;
there is no internal-DRAM cache on no-PSRAM devices.

The selected SD reader font may allocate one 1 MiB uninitialized PSRAM block.
It clears only its fixed glyph index and appends verified glyph bitmaps into the
remaining arena. The cache is optional, shared across styles of that one font,
and released on font unload; UI fallback fonts never allocate another block.

## ReadPico network memory checks

The E0470 waveform trimmer now computes sequence lengths in a first pass and
writes the trimmed waveform in a second pass, replacing the 17,408-byte static
workspace with bounded stack scratch. GC16/GL16 output remains byte-identical;
run the SDK's `libs/display/EpdiyLcd/test/host/test_waveform_trim.py` to check it.

Network entry, BLE shutdown, font reclamation, mode initialization, web-service
startup and exit log internal and internal-DMA free bytes/largest blocks plus
PSRAM free bytes through `HalMemory`. DMA memory overlaps internal memory;
these figures must not be added together. Startup failure returns immediately,
and WiFi scanning exposes explicit Retry and Back actions instead of reporting
zero networks or automatically repeating driver initialization.

Keep the ReadPico 32 KiB internal reserve and 4096-byte malloc preference threshold
while measuring the combined waveform and WiFi/LwIP PSRAM changes. Additional
PSRAM settings require independent device validation; a successful build alone
is not evidence that hotspot startup or transfer is reliable.

The combined waveform/network candidate started AP services, but left only
5,639 internal bytes (largest 3,316) and 2,183 internal-DMA bytes (largest 32);
the phone could not join. ReadPico therefore enables SDK network/Bluetooth BSS
relocation with `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`. This must be
paired with `CONFIG_SPIRAM_BOOT_HW_INIT=y` and `CONFIG_SPIRAM_BOOT_INIT=y`: the
Arduino defaults defer PSRAM setup, too late for external BSS. Enabling only BSS
relocation moved 15,088 bytes in the ELF but caused repeated startup panics on
the device; that failed image/log is retained for comparison.

Verify external BSS symbols in the final ELF and require cold boot, AP/STA,
transfer and BLE acceptance. Display buffers and task stacks keep their existing
allocation requirements. The ordinary malloc threshold, mDNS allocation policy
and NVS cache policy remain unchanged while testing this configuration.

### TLS preflight and PSRAM

Font manifest and OPDS download preflight use `HttpDownloader::hasMemoryForTls()`.
Its 40,000-byte free / 20,000-byte largest-block floors are measured through
`HalMemory::getDefaultHeap()`, matching the ordinary allocator used by wolfSSL.
Registered PSRAM counts; `ESP.getFreeHeap()` only reports internal RAM and can
reject a valid ReadPico transfer before it starts. No-PSRAM targets retain the
same floors. This check is a heuristic, not a reservation or a guarantee that
later TLS, driver, or parsing allocations will succeed. Failure logs distinguish
default, internal and PSRAM capacity; font phase logs locate subsequent failures.

### JPEG preflight and recoverable AirPage display failures

JPEG dimension probing, framebuffer decode and JPEG-to-BMP conversion use
`HalMemory::getDefaultHeap()` for ordinary `makeUniqueNoThrow<JPEGDEC>()`
allocations, including registered PSRAM. The unchanged floors are
`sizeof(JPEGDEC) + 16 KiB` free and `sizeof(JPEGDEC)` contiguous. Framebuffer
scratch loans still bypass this heap preflight. Actual allocation remains
fallible; diagnostics distinguish default, internal and PSRAM heaps.

AirPage keeps pending downloads, backups and history images when rendering
fails. JPEG preflight/allocation failures report insufficient memory; other
render failures report a retryable display failure, not proven corruption.
The history list includes a pending current image so it can be selected again
without downloading. Only successful display commits/archives the pending
image during the session; subsequent pushes and startup retain the existing
recovery policy. Header validation retains its existing invalid-file handling.
No decoder or full-screen buffer is retained between attempts, and internal
DMA reserves are unchanged. Verify repeated JPEG/BMP pushes, retries and sleep
wallpaper conversion on Read Pico; host tests cannot establish optical quality.

### AirPage image storage and paging

New AirPage downloads, current images (`latest.bmp`/`latest.jpg`), timestamp-named
archives and pixel caches share `/AirPage`. Existing `/.crosspoint/airpage`
images are neither migrated nor read; device preferences retain their hidden
paths. `/sleep.bmp` remains the installed wallpaper destination.

History files have no count-based retention limit. The store keeps one 20-entry
page, with current first on the first page and descending archive ID/format order.
Previous/next rows reuse the existing list controls. Page scans use fixed RAM;
latency grows with directory size because each page scans the files. Unknown
files and orphan caches are left untouched. Sequence allocation scans disk rather
than the current page to avoid overwriting older archives. Test more than 20
images, paging in both directions, restart, failed display/retry and full-SD
archive failure; a successful build does not verify touch or optical behavior.
