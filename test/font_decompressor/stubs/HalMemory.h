#pragma once
#include <cstddef>
extern bool testHasPsram;
class HalMemory {
 public:
  struct HeapStats {
    size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes;
  };
  static HeapStats getPsramHeap() { return {0, testHasPsram ? 8 * 1024 * 1024u : 0, 0, 0}; }
};
