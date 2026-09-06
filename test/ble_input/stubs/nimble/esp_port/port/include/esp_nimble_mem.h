#pragma once
#include <cstddef>
#include <cstdlib>
inline int probes = 0, probeFrees = 0;
inline bool probeFails = false;
inline char probeStorage[16];
inline void* nimble_platform_mem_malloc(size_t size) {
  if (size != 16) std::abort();
  ++probes;
  return probeFails ? nullptr : probeStorage;
}
inline void nimble_platform_mem_free(void* ptr) {
  if (ptr != probeStorage) std::abort();
  ++probeFrees;
}
