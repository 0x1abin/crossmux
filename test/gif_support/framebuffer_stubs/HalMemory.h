#pragma once

#include <cstddef>

class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes;
  };
  static HeapStats getInternalHeap() { return {1024 * 1024}; }
};
