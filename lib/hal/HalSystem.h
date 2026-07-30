#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

using DeviceId = std::array<uint8_t, 6>;
bool getDeviceId(DeviceId& out);

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
}  // namespace HalSystem
