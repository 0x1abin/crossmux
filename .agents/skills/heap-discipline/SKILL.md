---
name: heap-discipline
description: >-
  Memory allocation discipline for the ESP32-C3 (~380KB RAM, no PSRAM, single panel-sized framebuffer). Use whenever writing or reviewing code that allocates: new / malloc / std::vector / std::string, buffers, caches, or anything held across a loop or an activity lifecycle. Covers makeUniqueNoThrow vs raw new/malloc, fragmentation avoidance, reserve-before-push_back, alloc-once-reuse, stack vs heap sizing, and the chunked grayscale buffer pattern.
---

# Heap Discipline (ESP32-C3)

AGENTS.md states the allocation rules. This is the procedure you run while
writing the code and the gate you run before handing it back.

The constraint that makes every call matter: ~380KB RAM, no PSRAM, one panel-sized
framebuffer (48,000 bytes on X4; 52,272 on X3). **Fragmentation, not total usage, is what kills this device.**
Free-heap can read fine while the largest free block is too small for the next
allocation. Optimize for not leaving holes, not just for using fewer bytes.

## Allocation decision procedure

Ask in order; stop at the first yes.

1. **Stack?** Local, bounded, under ~256 bytes total: plain array/struct. No
   heap, no fragmentation. Keep frames lean; the task stack is small.
2. **Compile-time constant?** Use `static constexpr` when applicable; verify
   linker placement before claiming Flash storage or zero DRAM.
3. **Allocated once and reused for an activity's lifetime?** Allocate in
   `onEnter`, hold in a member, release in `onExit`. Never per-frame, never
   per-iteration.
4. **Dynamic and fallible?** `makeUniqueNoThrow<T>(...)` /
   `makeUniqueNoThrow<T[]>(n)` from `lib/Memory/Memory.h`. Null-check, `LOG_ERR`
   with the size, return false. It frees on every exit path.
5. **A C/SDK API takes ownership and frees it itself?** Only then raw
   `new (std::nothrow)` / `malloc`, with a comment naming who frees it.

Bare `new` / `new[]` is never correct here: under `-fno-exceptions` it calls
`abort()` on OOM instead of returning null.

## Fragmentation rules

- `std::vector`: `reserve(n)` before any `push_back` loop. Each growth is
  alloc-copy-free (three heap ops) and leaves a hole. Unknown n: estimate high.
- No repeated `new`/`delete` or growing containers inside a loop or render path.
  Hoist the allocation out of the loop.
- Large contiguous blocks fragment worst. Full-screen-class buffers use the
  chunked `storeBwBuffer` / `restoreBwBuffer` path in `GfxRenderer` so they
  never demand one contiguous 48KB block. Reuse that path. Do not malloc a
  second full-screen buffer.
- `std::string` / Arduino `String`: acceptable on cold paths (file I/O, one-shot
  setup). Banned on hot/render paths. Build text with a stack `char[]` +
  `snprintf`; if a `std::string` is unavoidable, `reserve` it first.

## Releasing capacity at phase boundaries

`clear()` destroys vector elements but retains capacity. `shrink_to_fit()` is
non-binding; the current ESP32 libstdc++ 14.2.0 does not reclaim vector storage
with `-fno-exceptions`. When an owned, default-allocator vector must return its
storage, use the existing empty-vector swap pattern after successful persistence.
Keep capacity for reuse on hot paths rather than repeatedly freeing it.

Test the real release method with normal host flags and `-fno-exceptions`, plus
the affected device branches. Check capacity, not just `empty()`, and cover save
failure retaining data, repeated release, cancellation and exit recovery.

Foreground network owners must save dirty data before unloading it, refuse
launch on save failure, and preserve their existing restore/return routes. Do
not move that opt-in policy into shared networking used by live/background apps.
Reuse the tests and acceptance procedure in
[download memory](../../../docs/engineering/network-memory-validation.md).

## Justify every allocation

Per AGENTS.md's evidence rule: when you add a heap allocation, state in one line
why stack/static/reuse was rejected and the worst-case size. If you cannot name
the size, you cannot budget it, and you should not allocate it.

## Self-review before handoff

- [ ] No bare `new`/`new[]`. Every fallible alloc is `makeUniqueNoThrow`, or a
      raw alloc with an explicit owner comment.
- [ ] Every allocation is null-checked with `LOG_ERR` before the error return.
- [ ] No allocation inside a loop or render path that could be hoisted.
- [ ] Every `push_back` loop has a preceding `reserve`.
- [ ] Anything allocated in `onEnter` is released in `onExit`; member `HalFile`
      closed there too.
- [ ] No second full-screen buffer; grayscale uses store/restoreBwBuffer.
- [ ] Each new allocation carries a one-line size + why-not-stack/static note.

Budget peak coexistence, including container replacement and pushed parent
Activities. Measure internal and PSRAM heaps plus largest blocks separately;
`HalMemory::allocatePsram()` fails without falling back to internal RAM. Use the
existing no-memory business fallback. Detailed placement and ownership contracts
live in [memory guidance](../../../docs/engineering/memory-and-allocation.md).
