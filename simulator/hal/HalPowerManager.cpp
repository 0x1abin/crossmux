// HAL power manager backend stub for the simulator.
// The host has no real power management — battery is fixed, "deep sleep" exits.

#include <HalPowerManager.h>

#include "../simulator_config.h"

void HalPowerManager::begin() {}
void HalPowerManager::setPowerSaving(bool) {}
void HalPowerManager::startDeepSleep(HalGPIO&) const { std::exit(0); }
uint16_t HalPowerManager::getBatteryPercentage() const { return SIMULATOR_BATTERY_PERCENT; }

HalPowerManager::Lock::Lock() {}
HalPowerManager::Lock::~Lock() {}

HalPowerManager powerManager;
