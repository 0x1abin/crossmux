# Hardware Constraints & The Resource Protocol

> Deep reference for [AGENTS.md](../../AGENTS.md). The ESP32-C3 baseline is about
> 380KB usable RAM without PSRAM; shared code must fit it. S3 budgets and
> capabilities are target-specific.

## Development Environment Awareness

**CRITICAL**: Detect the host platform at session start to choose appropriate tools and commands.

### Platform Detection
```bash
# Detect platform (run once per session)
uname -s
# Returns: MINGW64_NT-* (Windows Git Bash), Linux, Darwin (macOS)
```

**Detection Required**: Run `uname -s` at session start to determine platform

### Platform-Specific Behaviors
- **Windows (Git Bash)**: Unix commands, `C:\` paths in Windows but `/` in bash, limited glob (use repository wrappers)
- **Linux/WSL**: Full bash, Unix paths, native glob support

**Cross-Platform Code Formatting**:
```bash
./bin/clang-format-fix
```

---

## Platform and Hardware Constraints

### Xteink X4 / ESP32-C3 Baseline
* MCU: ESP32-C3 (Single-core RISC-V @ 160MHz)
* RAM: ~380KB usable (VERY LIMITED - primary project constraint)
  * **ESP32-C3 has no PSRAM** and remains the compatibility baseline
  * PSRAM availability is target-specific. Shared business logic needs a no-PSRAM
    fallback; an explicitly PSRAM-only allocation must fail without consuming internal RAM.
  * **Single Buffer Mode**: One framebuffer sized for the runtime panel. X4 uses
    48,000 bytes; X3 uses 52,272 bytes (792 × 528 ÷ 8).
* Flash: 16MB (Instruction storage and static data)
* Display: 800x480 E-Ink (Slow refresh, monochrome, 1-2s full update)
  * Framebuffer: 48,000 bytes (800 × 480 ÷ 8)
* Storage: SD Card (Used for books and aggressive caching)

### The Resource Protocol
1. Stack Safety: Limit local function variables to < 256 bytes. The ESP32-C3 default stack is small; use std::unique_ptr or static pools for larger buffers.
2. Heap Fragmentation: Avoid repeated new/delete in loops. Allocate buffers once during onEnter() and reuse them.
3. Flash Persistence: Give large immutable tables static storage and const qualification. Verify placement in the linker map before claiming DRAM savings; data read with the flash cache disabled must remain in accessible internal RAM.
4. String Policy: Prohibit std::string and Arduino String in hot paths. Use std::string_view for read-only access and snprintf with fixed char[] buffers for construction.
5. UI Strings: All user-facing text must use the `tr()` macro (e.g., `tr(STR_LOADING)`) for i18n support. Never hardcode UI strings directly. For the avoidance of doubt, logging messages (LOG_DBG/LOG_ERR) can be hardcoded, but user-facing text must use `tr()`.
6. `constexpr` First: Compile-time constants and lookup tables must be `constexpr`, not just `static const`. This enables constant evaluation and dead-branch elimination; it does not guarantee flash placement or eliminate runtime storage. Use `static constexpr` for class-level constants.
7. `std::vector` Pre-allocation: Always call `.reserve(N)` before any `push_back()` loop. Each growth event allocates a new block (2×), copies all elements, then frees the old one — three heap operations that fragment DRAM. When the final size is unknown, estimate conservatively.
8. SD Persistence Throttling: Settings, state and credentials live on SD under `/.crosspoint/` through `HalStorage`; SPIFFS is not mounted. Guard unchanged writes and debounce progress saves to avoid serialization, SD I/O and storage-mutex cost.
9. `new` is not nothrow on ESP32: With `-fno-exceptions`, bare `new` that fails calls `abort()` — it does NOT return `nullptr`. Always use `new (std::nothrow)` and null-check the result, or use `makeUniqueNoThrow<T>()` from `lib/Memory/Memory.h`. Never write bare `new` for any fallible allocation.

See also: [memory-and-allocation.md](memory-and-allocation.md) for allocation patterns, [esp32-pitfalls.md](esp32-pitfalls.md) for platform-specific hazards.

## Target-specific capability and peak budgets

Read the selected `platformio.ini` environment, SDK `BoardConfig`, and HAL before
assuming touch, frontlight, USB-MSC, usable PSRAM or a grayscale mode. X4 Classic
is button-only. An SDK profile or successful build establishes neither published
release support nor hardware acceptance; release membership comes from
[`scripts/nightly_targets.py`](../../scripts/nightly_targets.py).

Measure `HalMemory::getInternalHeap()` and `getPsramHeap()` separately, including
`largestBlockBytes`. Container growth, retained parent Activities and simultaneous
font/network/display resources contribute to peak use, even when the final object
fits. `HalMemory::allocatePsram()` returns null without an internal-RAM fallback;
preserve that contract and the existing low-memory business path.
