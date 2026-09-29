#include "HalTiltSensor.h"

#include <BoardConfig.h>
#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

namespace {

// Map the sensor's in-plane axes to the forward/back tilt convention, which is the
// gyro's: portrait tilts about X and uses `primary`, landscape about Y and uses
// `secondary`, and TILT_INVERTED flips the sign. Accelerometer boards pass the
// perpendicular axis as `primary`/`secondary` (see the call site).
float tiltAxisFor(const uint8_t mode, const uint8_t orientation, const float primary, const float secondary) {
  const bool inverted = mode == CrossPointTiltPageTurn::TILT_INVERTED;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      return inverted ? -primary : primary;
    case CrossPointOrientation::INVERTED:
      return inverted ? primary : -primary;
    case CrossPointOrientation::LANDSCAPE_CW:
      return inverted ? secondary : -secondary;
    case CrossPointOrientation::LANDSCAPE_CCW:
      return inverted ? -secondary : secondary;
    default:
      return primary;
  }
}

}  // namespace

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  gx = sample.gx;
  gy = sample.gy;
  gz = sample.gz;
  return true;
}

bool HalTiltSensor::readAccel(float& ax, float& ay, float& az) const {
  Imu::Sample sample;
  if (!_sdkImu.read(sample)) return false;
  ax = sample.ax;
  ay = sample.ay;
  az = sample.az;
  return true;
}

void HalTiltSensor::begin() {
  _available = _sdkImu.begin();
  if (!_available) {
    LOG_ERR("GYR", "SDK IMU not found");
    return;
  }

  // The SC7A20H (Read Pico) is a 3-axis accelerometer with no gyroscope: read() fills
  // ax/ay/az but reports gx = gy = gz = 0, so the angular-rate gesture below could never
  // fire. The same flick is still measurable -- see RATE_THRESHOLD_GPS in the header --
  // by differentiating the gravity component, so the part is brought up like any other
  // and update() takes the accelerometer path. isAvailable() then reports true, which is
  // the gate SettingsList uses to offer "Tilt page turn".
  _accelOnly = BoardConfig::ACTIVE.sensors.imuType == BoardConfig::ImuType::Sc7a20h;

  _initMs = millis();
  _lastPollMs = millis();
  // begin() leaves the sensors sampling; stand them by until tilt page turn actually
  // wakes them, so a disabled IMU doesn't drain the battery.
  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU standby failed");
  }
  LOG_INF("GYR", "SDK IMU initialized (%s)", _accelOnly ? "accelerometer only, gravity rate" : "gyroscope rate");
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.wake()) {
    LOG_ERR("GYR", "IMU wake failed");
    return false;
  }

  _lastPollMs = millis();
  _lastTiltMs = millis();
  _wakeMs = millis();
  _isAwake = true;
  return true;
}

bool HalTiltSensor::deepSleep() {
  if (!_available) {
    return false;
  }

  if (!_sdkImu.sleep()) {
    LOG_ERR("GYR", "IMU sleep failed");
    return false;
  }

  clearPendingEvents();
  _inTilt = false;
  _isAwake = false;
  return true;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  if (!_available) {
    return;
  }

  const bool shouldBeAwake = mode != CrossPointTiltPageTurn::TILT_OFF && inReader;
  if (shouldBeAwake && !_isAwake) {
    _isAwake = wake();
    return;
  }
  if (!shouldBeAwake && _isAwake) {
    _isAwake = !deepSleep();
    return;
  }

  if (!shouldBeAwake) {
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  // Gyro boards read an angular rate directly; accelerometer boards differentiate the
  // gravity component, so the two arrive here in different units and against thresholds
  // converted from the same 270 dps. See RATE_THRESHOLD_GPS in the header.
  float tiltAxis;
  float rateThreshold;
  float neutralRate;
  if (_accelOnly) {
    float ax, ay, az;
    if (!readAccel(ax, ay, az)) {
      return;
    }
    // The gyro measures angular rate ABOUT the tilt axis; the accelerometer measures the
    // gravity component ALONG the axis perpendicular to it. The gesture is a LEFT/RIGHT
    // sway, so in portrait the device rolls about its up/down axis and gravity swings
    // along the left/right axis (ax); landscape swaps which axis is which, so it swings
    // along ay. Mapping the gyro axis straight across would fire on the wrong motion.
    //
    // The sign is flipped because the accelerometer's gravity swing runs opposite to the
    // gyro's rotation rate for the same physical sway; flipping here rather than inside
    // tiltAxisFor() leaves the gyro boards' convention exactly as it was.
    const float sample = -tiltAxisFor(mode, orientation, ax, ay);
    tiltAxis = (_lastTiltGMs != 0 && now > _lastTiltGMs)
                   ? (sample - _lastTiltG) * 1000.0f / static_cast<float>(now - _lastTiltGMs)
                   : 0.0f;
    _lastTiltG = sample;
    _lastTiltGMs = now;
    rateThreshold = RATE_THRESHOLD_GPS;
    neutralRate = NEUTRAL_RATE_GPS;
  } else {
    float gx, gy, gz;
    if (!readGyro(gx, gy, gz)) {
      return;
    }
    // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
    tiltAxis = tiltAxisFor(mode, orientation, gx, gy);
    rateThreshold = RATE_THRESHOLD_DPS;
    neutralRate = NEUTRAL_RATE_DPS;
  }

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < neutralRate) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > rateThreshold) {
        _tiltForwardEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Forward Trigger=(%.1f) %s", tiltAxis, _accelOnly ? "g/s" : "dps");
      } else if (tiltAxis < -rateThreshold) {
        _tiltBackEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Backward Trigger=(%.1f) %s", tiltAxis, _accelOnly ? "g/s" : "dps");
      }
    }
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}
