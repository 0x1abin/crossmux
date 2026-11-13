# Metalio Plus frontlight implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline in the existing
> isolated checkout. The user authorizes continued porting and explicitly defers
> USB reconnection debugging. No new process approval, commit or restoration.

**Goal:** Working bounded cool/warm frontlight through existing CrossMux UI.

**Architecture:** Enable Plus capabilities and profile GPIO6/7 at 20kHz/10-bit.
An optional trailing FrontlightConfig PWM ceiling defaults to 1000 permille;
Plus uses 100. The real SDK PWM backend applies it to total duty before the warm/cool mix, including
kick-start, while preserving other board outputs and active-low off levels.

**Tech Stack:** Existing Arduino4/IDF6.1 S31, C++ real-driver host recording API.

**Spec:** ../specs/2026-11-08-metalio-plus-frontlight.md

## Global constraints

- V1.4 S31, GPIO6 cool / GPIO7 warm; PWM outputs at most 102/1023 per channel.
- Preserve 74 frozen vendor files and committed SDK gitlink.
- No new dependency, I2C bus, task or firmware heap allocation.
- Keep active original recovery manifest and snapshots; restore only on request.

## Review focus

- All PWM writes, minimum nonzero output and kick-start obey the ceiling.
- Active-low physical off stays hardware-full; existing profiles default to 100%.
- Sleep forces both pads low; wake releases holds before attaching channels.
- Controls and settings remain the existing reader flow; no duplicate light UI.

### Task 1: Profile and bounded PWM with real driver tests

Files: SDK BoardConfig.h, FrontlightManager.cpp, its test/host/run_metalio_plus.py
and test_metalio_plus.cpp; root test/CMakeLists.txt.

- [x] RED: real driver and real profile fail absent Plus light capability.
- [x] Enable Plus profile/capability and a backward-compatible output ceiling.
- [x] RED: observe uncapped driver exceed 102 duty before applying the ceiling.
- [x] GREEN: brightness/level/warmth sweeps, off/polarity/kick/hold recovery and
  existing PWM/I2C boards retain behavior; no test-only production hooks.

### Task 2: Build and physical acceptance

- [x] Build S31; archive unique BIN/ELF/map, hashes and native config checks.
- [x] Run affected profile/manager and required repository checks; record
  pre-existing full-CI limitations separately; review the focused diff.
- [x] Update retained reader preserving original recovery association.
- [x] User verifies light range, color, off, persistence and sleep recovery.
- [x] Record evidence and update engineering docs/README; record measured current as pending; the subsequent read-only battery increment is accepted.

Review found a low-duty quantization issue when limiting each mixed channel
separately. A new constant-total assertion failed (mix RED); the implementation
now scales total duty before mixing and passes for all brightness/level/warmth
combinations on Plus and the X4Pro regression. The first bounded artifact was
not programmed and its trial plan is disabled; only the corrected balanced
artifact is eligible for device programming.

Final physical result: user confirmed 正常 after the explicit light sleep/wake,
page turn and brightness/warmth retention procedure. Evidence is parent
logs/plus-frontlight-sleep-acceptance.json. Serial capture ended intentionally;
current CrossMux and original recovery association remain retained.
