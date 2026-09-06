#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2, MALLOC_CAP_8BIT = 4;
inline size_t freeInternal = 100 * 1024, largestInternal = 40 * 1024;
inline size_t freePsram = 512 * 1024, largestPsram = 128 * 1024;
inline size_t heap_caps_get_free_size(uint32_t caps) { return caps & MALLOC_CAP_INTERNAL ? freeInternal : freePsram; }
inline size_t heap_caps_get_largest_free_block(uint32_t caps) {
  return caps & MALLOC_CAP_INTERNAL ? largestInternal : largestPsram;
}
inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
inline size_t heap_caps_get_total_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
