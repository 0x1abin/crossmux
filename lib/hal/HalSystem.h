#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

struct HeapInfo {
  uint32_t freeBytes;
  uint32_t totalBytes;
  uint32_t largestFreeBlockBytes;
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

using DeviceId = std::array<uint8_t, 6>;
const char* getDeviceModel();
bool getDeviceId(DeviceId& out);
bool getWifiStationMac(DeviceId& out);
bool getChipTemperatureCelsius(float& out);
uint64_t getUptimeSeconds();
HeapInfo getHeapInfo();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
