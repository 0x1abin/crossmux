#pragma once

// Shim: lib/hal/HalPowerManager.h includes this header. The real BatteryMonitor is
// only used inside HalPowerManager.cpp, which we replace with hal/HalPowerManager.cpp.
// An empty class is enough to satisfy the include.

class BatteryMonitor {
 public:
  BatteryMonitor() = default;
};
