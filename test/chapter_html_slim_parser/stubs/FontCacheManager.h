#pragma once
#include <Arduino.h>
class FontCacheManager {
 public:
  int releases = 0;
  uint32_t reclaimedBytes = 0;
  void releaseSdFontCaches() {
    ++releases;
    ESP.freeHeap += reclaimedBytes;
    reclaimedBytes = 0;
  }
};
