// HAL system stub for the simulator. Real HalSystem captures CPU panic info via
// ESP-IDF private headers; on the host there are no panics in that sense.

#include <HalSystem.h>

namespace HalSystem {

void begin() {}
void checkPanic() {}
void clearPanic() {}
std::string getPanicInfo(bool /*full*/) { return std::string(); }
bool isRebootFromPanic() { return false; }
bool getDeviceId(DeviceId& out) {
  out = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  return true;
}

}  // namespace HalSystem
