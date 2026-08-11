#include "HalFrontlight.h"

#include <Logging.h>

HalFrontlight& HalFrontlight::getInstance() {
  static HalFrontlight instance;
  return instance;
}

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  manager.begin();
  setWarmth(warmth);
  setBrightness(on ? brightness : 0);
  if (manager.present()) {
    LOG_INF("LIGHT", "Frontlight initialized: brightness=%u warmth=%u", brightness, warmth);
  }
}
