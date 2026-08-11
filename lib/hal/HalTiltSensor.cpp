#include "HalTiltSensor.h"

HalTiltSensor halTiltSensor;  // Singleton instance

// EEGO A4 has no IMU, so every tilt operation is a no-op. All methods are
// defined out-of-line so the symbols exist for the app layer even though the
// hardware is absent.

void HalTiltSensor::begin() {}

bool HalTiltSensor::wake() { return false; }

bool HalTiltSensor::deepSleep() { return false; }

void HalTiltSensor::update(const uint8_t, const uint8_t, const bool) {}

bool HalTiltSensor::wasTiltedForward() { return false; }

bool HalTiltSensor::wasTiltedBack() { return false; }

bool HalTiltSensor::hadActivity() { return false; }

void HalTiltSensor::clearPendingEvents() {}