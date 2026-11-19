#pragma once
#include <cstddef>
namespace fake {
inline size_t vocabularyFreeHeap = 1000000;
inline size_t vocabularyLargestBlock = 1000000;
}  // namespace fake
struct HalMemory {
  struct Heap {
    size_t freeBytes, largestBlockBytes;
  };
  static Heap getDefaultHeap() { return {fake::vocabularyFreeHeap, fake::vocabularyLargestBlock}; }
};
