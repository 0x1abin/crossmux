#pragma once
#include "activities/RenderLock.h"
#include "esp_heap_caps.h"
class FontCacheManager {
 public:
  int releases = 0, clears = 0;
  size_t recoveredInternal = 0, recoveredLargest = 0;
  bool recoveredUnderLock = false;
  void releaseSdFontCaches() {
    recoveredUnderLock = renderLockDepth == 1;
    ++releases;
    freeInternal += recoveredInternal;
    largestInternal += recoveredLargest;
  }
  void clearCache() {
    recoveredUnderLock = renderLockDepth == 1;
    ++clears;
    freeInternal += recoveredInternal;
    largestInternal += recoveredLargest;
  }
};
