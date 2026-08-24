# Memory Safety & Heap Allocation

> Deep reference for [CLAUDE.md](../../CLAUDE.md). On ESP32-C3 with
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
