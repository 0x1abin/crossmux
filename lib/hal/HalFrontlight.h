#pragma once

#include <FrontlightManager.h>

#include <cstdint>

class HalFrontlight {
 public:
  static HalFrontlight& getInstance();

  void begin(uint8_t brightness, uint8_t warmth, bool on);

  void setBrightness(uint8_t percent) { manager.setBrightness(percent); }
  void setWarmth(uint8_t warmPercent) { manager.setColorTemperature(warmPercent); }
  void setOn(bool on) { on ? manager.on() : manager.off(); }

 private:
  FrontlightManager manager;
};

#define Frontlight HalFrontlight::getInstance()
