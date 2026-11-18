# Metalio Plus native IMU and rotation implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Supply a native SC7A20H backend and opt-in, stable automatic reader rotation.

**Architecture:** Hardware stays in SDK MetalioPlusIo/Imu. One HalTiltSensor poll
feeds tilt and a pure orientation classifier. ReaderActivity consumes a stable
event using its existing orientation/reflow path.

**Tech Stack:** Existing native IDF I2C owner, C++17/20, host ASan/UBSan, existing i18n generator.

**Spec:** [IMU design](../specs/2026-11-13-metalio-plus-imu-design.md).

## Global Constraints

- No flash, serial probing, SD/device settings changes or physical acceptance while away.
- No new dependencies without existing authorization; all installations stay in the independent dev directory.
- Continue the user-selected yios feature checkout; retain main SDK gitlink baseline; no implicit commits/pushes.
- Native bus1 SDA0/SCL1/100kHz, 0x19, WHO11/VERSION28; preserve GPIO2 IRQ routes, FIFO and shared clients.
- Auto rotation defaults off; 600ms dwell/1200ms cooldown; no new task/application heap allocation.

## Review Focus

- Unknown identity, IRQ routing, FIFO/filter settings must refuse activation without destructive writes.
- A partly failed wake must attempt standby and never report a fresh sample from a failed read.
- Flat/noisy or stale samples must not rotate, including unsigned timer wrap.
- Reading overlays, held keys, touch gestures and reader exit must cancel inherited rotation intent.
- Tilt and rotation must share one sample and power lifecycle; C3/ReadPico behavior must stay unchanged.

### Task 1: Native SC7A20H transport and profile

**Files:** SDK MetalioPlusIo.{h,cpp}, Imu/src/Imu.cpp, BoardConfig.h,
BoardConfig/test/host/run_metalio_plus.py; new Imu/test/host/{run_metalio_plus.py,test_metalio_plus.cpp}.

**Interfaces:** `metalioPlusImuBegin()`, `metalioPlusImuWake()`,
`metalioPlusImuSleep()`, `metalioPlusImuRead(int16_t* axes)` with unchanged output on failure.
Imu's existing public API remains the consumer; normalized raw scale is 1/16384g.

- [x] Add a production Imu/native recording harness; watch native-routing assertion fail against Wire.
- [x] Implement guarded native probe/power/read paths and profile; no IRQ/reset/FIFO/filter writes.
- [x] Cover begin/wake/read/sleep failure points, identity/refusal/retry and preserved outputs/registers.
- [x] Run new native suite, existing Imu suite, RTC/battery/native I/O and real profiles; record exact results.
  Expected: new native suite and focused existing suites pass; known baseline limitations remain named.

### Task 2: Shared sampling and stable orientation policy

**Files:** new lib/hal/AutoRotation.h; HalTiltSensor.{h,cpp}; test/hal_tilt and test CMake entry.

**Interfaces:** `AutoRotation::update(x,y,z,now,current)` returns an optional orientation;
`reset()` clears dwell/cooldown. `HalTiltSensor::update(..., bool autoRotate=false)`
and `takeOrientation(uint8_t&)` share the existing tilt sample and owner.

- [x] Test real classifier/HAL: four poses, noise/flat/nonfinite, drifted magnitude, dwell, cooldown,
  uint32 wrap, manual orientation, disable/exit and sample failures. Watch absent behavior fail.
- [x] Implement allocation-free classifier and optional HAL path; retain old three-argument callers.
- [x] Run classifier/native/old tilt suites. Expected: rotation and tilt coexist without duplicate reads/events.

### Task 3: Reader, settings and return checklist

**Files:** ReaderActivity.{h,cpp}, main.cpp, CrossPointSettings.h, SettingsList.{h,cpp},
english/chinese translation sources, generated via generator only; relevant test/CMake and docs.

**Interfaces:** persisted `autoRotate` toggle; main supplies foreground-reader eligibility;
ReaderActivity consumes HAL event only when a completed page owns input.

- [x] Add reader gating/persistence tests using real production helper boundaries; watch missing integration fail.
- [x] Add translated setting, guarded foreground polling and reuse manual orientation/reflow path.
- [x] Run focused integration/settings/i18n checks and full host CTest; identify any prior failing targets by name.
- [x] Record software status, candidate/source provenance and pending identity/axes/power/touch acceptance.
  Expected: no device writes; ready for the second requested subsystem, with physical acceptance pending.

Commit steps are deferred until explicitly requested by the user.
