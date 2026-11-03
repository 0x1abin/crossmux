#include <HalTiltSensor.h>

#include <cassert>

unsigned long clockMs = 1000;
Imu::Sample Imu::sample{};
int Imu::reads = 0;
bool Imu::readOk = true;

int main() {
  HalTiltSensor sensor;
  sensor.begin();
  auto update = [&](unsigned long time, uint8_t mode = CrossPointTiltPageTurn::TILT_NORMAL,
                    uint8_t orientation = CrossPointOrientation::PORTRAIT, bool reader = true) {
    clockMs = time;
    sensor.update(mode, orientation, reader);
  };
  auto noEvent = [&] { assert(!sensor.wasTiltedForward() && !sensor.wasTiltedBack()); };
  update(1000);
#if FREEINK_DEVICE_READPICO
  Imu::sample.ax = 1;
  update(1700);  // first sample after wake establishes the baseline
  noEvent();
  const int first = Imu::reads;
  update(1750);  // sensor has not reached its next 80 ms sample
  assert(Imu::reads == first);
  Imu::sample.ax = 0.5f;
  update(1780);
  assert(sensor.wasTiltedForward());
  noEvent();     // one event per flick
  update(1860);  // repeated gravity sample is neutral
  noEvent();
  Imu::sample.ay = -1;
  update(2600, CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::LANDSCAPE_CW);
  noEvent();  // changing axis must not differentiate it against the previous axis
  update(2680, CrossPointTiltPageTurn::TILT_INVERTED, CrossPointOrientation::LANDSCAPE_CW);
  noEvent();
  update(2800, CrossPointTiltPageTurn::TILT_NORMAL, CrossPointOrientation::PORTRAIT, false);
  Imu::sample.ax = -1;
  update(4000);
  update(4700);
  noEvent();  // changed pose while asleep is a new baseline
  Imu::sample.ax = -0.5f;
  update(4780);
  assert(sensor.wasTiltedBack());
  update(4860);
  Imu::readOk = false;
  update(6000);
  Imu::readOk = true;
  Imu::sample.ax = 1;
  update(6080);
  noEvent();  // a missing sample invalidates the derivative baseline
  update(7000, CrossPointTiltPageTurn::TILT_OFF);
  Imu::sample.ax = -1;
  update(8000);
  update(8700);
  noEvent();
#else
  Imu::sample.gx = 400;
  update(1700);
  assert(sensor.wasTiltedForward());
  const int first = Imu::reads;
  update(1749);
  assert(Imu::reads == first);
  Imu::sample.gx = 0;
  update(1750);
  assert(Imu::reads == first + 1);  // retain existing 50 ms gyro cadence
  Imu::sample.gx = -400;
  update(2400);
  assert(sensor.wasTiltedBack());
  noEvent();
#endif
}
