# Metalio Plus native IMU and automatic reading orientation

The user explicitly ordered IMU/rotation, native sixteen-gray, cover/image gray,
then build-environment repair, and delegated development while away. Physical
acceptance starts only after return. This is the first independent subsystem.
Implementation details below are engineering decisions under that delegation;
no new design approval, implicit commit, push or device write is required.

## Evidence and approach

- Matching V1.4 reference config lines 87–94 places SC7A20H at 0x19/0x18 and
  wires INT1 with TCA INT to GPIO2. The previous address-only probe saw 0x19;
  silicon identity and actual mounting axes remain physically unverified.
- SDK Imu.cpp:230 configures SC7A20H through Wire and clears IRQ routing;
  MetalioPlusIo.cpp:50 already owns native bus1 SDA0/SCL1 at100kHz.
- HalTiltSensor.cpp:118 polls for tilt only; accelerometer-only handling is
  currently ReadPico-only. ReaderActivity.cpp:314 already applies orientation,
  and EPUB detects external settings changes and preserves content position.

Use a Plus-only native backend and the existing Imu/HAL APIs. Directly enabling
Wire would compete for the bus. An interrupt-based orientation service would
change the accepted power-wake contract, so use foreground polling instead.
No new worker task, library or full-screen buffer is needed.

## Hardware contract

Only 0x19 is configured, with WHO_AM_I=0x11 and VERSION=0x28 as the SDK's existing
SC7A20H decoder requires. Unknown identity, unsupported revision, unavailable bus,
nonzero CTRL3/CTRL6 interrupt routing, enabled FIFO or high-pass filtering causes
controlled refusal without rewriting those registers. No reset, interrupt-route,
charger, gauge, RTC or frozen vendor changes. Begin validates then requests ODR
power-down; wake configures CTRL0 high resolution, CTRL4 BDU/±2g and CTRL1 100Hz,
under the existing mutex, with bounded power-down cleanup after failure.
Status plus six acceleration bytes are read while locked; no-data/failure keeps
the caller's output unchanged. No gyro output is fabricated. Axis conventions
use board mounting flags; identity mapping is provisional until four-way bench
acceptance. The candidate remains off-device while the user is away.

## Orientation policy

Add a persisted, translated `autoRotate` toggle, default off, exposed only for
the Plus native accelerometer. Tilt and rotation share one sample/power owner.
Polling is active only when a foreground reader requests either feature. No
rotation in menus, popups, transfers or sleep; no sensor event is a physical key.

A small allocation-free classifier requires 600ms of a stable dominant gravity
axis and 1200ms between changes. Reject non-finite data, gravity magnitude
outside 0.75–1.25g, |z|>0.8g, dominant in-plane component <0.7g, or separation
between in-plane axes <0.25g. Portrait is y<0, clockwise x>0, inverted y>0,
counterclockwise x<0 after mounting transforms. Reset dwell across invalid
samples, disable, reader exit, manual orientation changes and sampling failures.
Use unsigned elapsed time for timer wrap. Each accepted pose produces one event.

ReaderActivity uses the existing orientation path, gated by a completed page,
no overlay/render work, and no held/edge/touch gesture. EPUB preserves content
position and reflows through its existing external-orientation detection. XTC
uses its existing manual rotation path. Do not rotate the home/settings layout.
Settings serialize by named key, so no binary cache format change is needed.

## Verification and limits

Compile production Imu/native IO with a recording I2C boundary, covering no Wire,
identity/config refusal, full/partial transfer failures, unchanged samples,
power lifecycle and preservation of shared IRQ registers. Exercise the real
classifier/HAL for four poses, jitter, flat/invalid samples, wrap, cooldown,
disabled states, one sample owner, tilt regression and stale event cancellation.
Check reader gating/settings translation and existing reader/tilt regressions.
Build only after later requested environment repair if blocked. Actual identity,
axes, orientation/touch, progress, power wake and current remain pending until
the user's return. Never label host tests as physical acceptance.
