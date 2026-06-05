#pragma once

// Host shim for ESP-IDF's <esp_heap_caps.h>. The native/WASM simulator has no
// ESP-IDF multi-heap allocator; firmware only reaches for heap_caps_* here to
// log diagnostic heap figures, so we mirror the flat host-heap estimate the
// esp_system shim already reports via ESP.getMaxAllocHeap().

#include <esp_system.h>  // EspClass ESP — backs the host heap estimate

#include <cstddef>
#include <cstdint>

// Capability bitflags. Values mirror ESP-IDF so MALLOC_CAP_* expressions keep
// the same numeric meaning; the host allocator ignores them entirely.
#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

// Largest allocatable block for the requested capabilities. The host has a
// single flat heap, so report the same value getMaxAllocHeap() does regardless
// of `caps` (which is unused on host).
inline size_t heap_caps_get_largest_free_block(uint32_t /*caps*/) { return static_cast<size_t>(ESP.getMaxAllocHeap()); }

// Total free heap for the requested capabilities — kept for parity with the
// IDF API in case future firmware uses it; mirrors ESP.getFreeHeap() on host.
inline size_t heap_caps_get_free_size(uint32_t /*caps*/) { return static_cast<size_t>(ESP.getFreeHeap()); }
