// HAL tilt sensor stub for the simulator. The host has no IMU; tilt-page-turn is disabled.

#include <HalTiltSensor.h>

void HalTiltSensor::begin() {}
bool HalTiltSensor::wake() { return false; }
bool HalTiltSensor::deepSleep() { return false; }
void HalTiltSensor::update(const uint8_t, const uint8_t, const bool) {}
bool HalTiltSensor::wasTiltedForward() { return false; }
bool HalTiltSensor::wasTiltedBack() { return false; }
bool HalTiltSensor::hadActivity() { return false; }
void HalTiltSensor::clearPendingEvents() {}

HalTiltSensor halTiltSensor;
