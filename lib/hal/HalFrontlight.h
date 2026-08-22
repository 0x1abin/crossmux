#pragma once

#include <FrontlightManager.h>

#include <cstdint>

class HalFrontlight {
 public:
  static HalFrontlight& getInstance();

  void begin(uint8_t brightness, uint8_t warmth, bool on);

  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  bool isOn() const { return lit; }

 private:
  HalFrontlight() = default;

  FrontlightManager manager;
  // The SDK represents off as brightness 0. Keep the selected brightness so
  // toggling back on restores it.
  uint8_t lastBrightness = 60;
  bool lit = false;
};

#define Frontlight HalFrontlight::getInstance()
