#include <HalTiltSensor.h>

#include <cassert>

#include "../../src/activities/reader/ReaderAutoRotation.h"

unsigned long clockMs = 1000;
Imu::Sample Imu::sample{};
int Imu::reads = 0;
bool Imu::readOk = true;

int main() {
  HalTiltSensor sensor;
  sensor.begin();
  assert(!sensor.isAvailable());
  assert(sensor.supportsAutomaticRotation());
  uint8_t pose = 99;
  auto step = [&](unsigned long at, bool enabled, bool reader = true, uint8_t tilt = 0, uint8_t current = 0) {
    clockMs = at;
    sensor.update(tilt, current, reader, enabled);
  };
  step(1000, false);
  step(1050, false, true, 1);  // Existing tilt mode alone must not enable a new Plus flick feature.
  assert(Imu::reads == 0 && !sensor.takeOrientation(pose) && pose == 99);
  step(1100, true);
  Imu::sample.ax = 1;
  step(1400, true);
  assert(Imu::reads == 1 && !sensor.takeOrientation(pose));
  step(1450, true);
  assert(Imu::reads == 1);
  for (unsigned t = 1500; t <= 2000; t += 100) step(t, true);
  assert(Imu::reads >= 2 && sensor.takeOrientation(pose) && pose == 1);
  assert(!sensor.takeOrientation(pose));
  assert(!sensor.wasTiltedForward() && !sensor.wasTiltedBack());
  step(2080, false);
  const int stoppedReads = Imu::reads;
  step(2900, false);
  assert(Imu::reads == stoppedReads);

  step(3000, true, true, 1, 1);
  Imu::sample.ax = 0;
  Imu::sample.ay = -1;
  step(3300, true, true, 1, 1);
  const int firstBoth = Imu::reads;
  step(3900, true, true, 1, 1);
  assert(Imu::reads ==
         firstBoth + 1);  // A saved tilt preference does not introduce a second read or a new flick feature.
  step(4000, true, false, 1, 1);
  assert(!sensor.takeOrientation(pose));  // Exiting a reader cancels even an unconsumed event.

  step(5000, true);
  Imu::sample.ax = 1;
  Imu::sample.ay = 0;
  step(5300, true);
  Imu::readOk = false;
  step(5800, true);
  Imu::readOk = true;
  step(5900, true);
  assert(!sensor.takeOrientation(pose));
  for (unsigned t = 6000; t < 6499; t += 100) step(t, true);
  step(6499, true);
  assert(!sensor.takeOrientation(pose));
  step(6600, true);
  assert(sensor.takeOrientation(pose) && pose == 1);
  step(6700, false);
  assert(!sensor.takeOrientation(pose));
  for (int blocked = 0; blocked < 3; ++blocked) {
    const unsigned start = 10000 + blocked * 4000;
    ReaderRotationContext context{true, true, false, false, false, 0, 1};
    auto sample = [&](unsigned at) {
      clockMs = at;
      sensor.update(0, 0, true, true, readerRotationInputAvailable(context));
    };
    sample(start);
    for (unsigned t = start + 300; t <= start + 600; t += 100) sample(t);
    if (blocked == 0) context.overlay = true;
    if (blocked == 1) context.gesture = true;
    if (blocked == 2) context.renderBusy = true;
    for (unsigned t = start + 700; t <= start + 900; t += 100) {
      sample(t);
      assert(!sensor.takeOrientation(pose));
    }
    context.overlay = context.gesture = context.renderBusy = false;
    for (unsigned t = start + 1000; t < start + 1600; t += 100) {
      sample(t);
      assert(!sensor.takeOrientation(pose));
    }
    sample(start + 1600);
    assert(sensor.takeOrientation(pose) && pose == 1);
    step(start + 1700, false);
  }
}
