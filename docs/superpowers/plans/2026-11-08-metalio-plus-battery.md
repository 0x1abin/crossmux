# Metalio Plus Battery Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline in the existing
> isolated checkout. Continued BSP porting is authorized. No commits or restores.

**Goal:** Read-only battery voltage/SOC/current-derived charging in existing UI.
**Architecture:** SDK Plus profile + BatteryMonitor native read route + a locked,
one-second board snapshot; no application device-specific UI or new bus/task.
**Tech Stack:** Existing Arduino4/IDF6.1 S31 and real C++ host harness.
**Spec:** ../specs/2026-11-08-metalio-plus-battery.md

## Global constraints

- V1.4 S31, native I2C1 SDA0/SCL1 100kHz; BQ27220 0x55.
- Charger address and design capacity stay zero; no writes or Wire initialization.
- Preserve frozen 74 vendor blobs, SDK gitlink, original recovery and SD backup.
- Restore only on user request; USB intermittent replug work deferred.

## Review focus

- Concurrent reads/cache share native mutex; no nested acquisition or reset.
- Signed -1mA stays valid; bad voltage/SOC and expired failed samples stay unknown.
- Two commands per second even with duplicate UI reads and failed transfers.
- First use and millis wrap work; no initialization before native bus ownership.
- Other gauge/ADC and PWM boards retain existing behavior.

### Task 1: Native battery backend

- [x] RED: compile/run real BatteryMonitor + native IO against fake native bus;
      observe unsupported profile/missing native route failures.
- [x] Enable profile and minimal native cached read-only backend.
- [x] GREEN: normal/endian/current/cache/wrap/lock/failure/invalid-field cases;
      other battery and profile harnesses pass.

### Task 2: Build and physical acceptance

- [x] Build S31, unique BIN/ELF/map archive, native image/config checks.
- [x] Affected and full host checks; focused reviewer, baseline limits recorded.
- [x] Update-and-keep original active manifest; inspect live battery data.
- [x] User verifies battery display and frontlight sleep/settings behavior.
- [x] Record measured scope and remaining unmeasured accuracy/power items.

Validation: 22 real Plus gauge cases, complete legacy BatteryMonitor harness,
Plus/C3/S3 profiles and native input/light/power tests pass. Boot policy tests
execute the actual HAL USB helper/guards and wake switch; two functional REDs
and the missing-helper guard RED were fixed and independently reviewed. Full
host suite 1531/1537, same six baseline failures; no new failures. S31 static
RAM 107968 bytes (+40), linked Flash 6612316 bytes. Unique native-battery image
archived and verified; full-CI matrix/static-analysis limitations remain as before.

User accepted battery display 97% and basic reading/page turns. Runtime sample
242995ms: 97%, 4375mV known, charging true known. Frontlight sleep/settings acceptance subsequently passed one explicit cycle.
No SOC-accuracy or consumption claim.

Final physical result: user confirmed 正常 after the explicit light sleep/wake,
page turn and brightness/warmth retention procedure. Evidence is parent
logs/plus-frontlight-sleep-acceptance.json. Serial capture ended intentionally;
current CrossMux and original recovery association remain retained.
