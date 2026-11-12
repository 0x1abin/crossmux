# Metalio Plus RTC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Reuse native I2C1 for safe PCF8563 timekeeping without competing Wire initialization.

**Architecture:** Plus transport functions in MetalioPlusIo own atomic clock reads
and explicit stop/write/restart. Rtc retains public DateTime/HalClock integration,
using a guarded native path while all other platform transports remain unchanged.

**Tech Stack:** Native IDF6.1 I2C owner, C++ RTC library, recording host transport.

**Spec:** [RTC design](../specs/2026-11-10-metalio-plus-rtc-design.md).

## Global Constraints

- PCF8563@0x51 / SDA0/SCL1, bus1/100kHz; no new bus or alarm wake.
- No dependency, new application buffer/task, charger/gauge/VCOM or frozen vendor
  changes; the existing native owner lazily allocates one cached RTC device handle.
- No flash/manual acceptance while user away; retain original recovery association.
- User delegated details/continuous execution; no additional approval or implicit commit/push.

## Review Focus

- A stopped/test/VL/invalid calendar must never restore plausible but wrong time.
- A failed partial set must not expose mixed time after a reboot.
- Initialization must preserve alarm, CLKOUT, timer and unrelated bus clients.
- Retry before native bus readiness must not create/reset it or latch absence.
- CrossMux startup must use the existing boot rail owner's initialized bus before trusted time.

### Task 1: Native transport and RTC behavior

**Files:** SDK `MetalioPlusIo.{h,cpp}`, `Rtc/src/Rtc.cpp`,
`Rtc/test/host/run_metalio_plus.py`, `Rtc/test/host/test_metalio_plus.cpp`.

**Interfaces:** `bool metalioPlusRtcRead(uint8_t reg,uint8_t* data,size_t size)`
for control0/time reads; `bool metalioPlusRtcWriteTime(const uint8_t* bcd)` for
the seven time bytes. Existing `Rtc::begin/now/set` public signatures stay unchanged.

- [x] Compile actual Rtc and native IO against recording transfer functions;
  assert native-only read, read-only begin, coherent date/BCD failures, leap dates,
  atomic set bounds/control preservation and every transfer/lock failure.
- [x] Watch native-routing assertion fail before implementing; then implement
  the smallest guarded transport/codec changes and run all cases green.
- [x] Run existing RTC, PCF85063, battery and native bus/boot suites.

### Task 2: Profile, integration and preserved candidate

**Files:** SDK `BoardConfig.h`, Plus profile test, test CMake entry and device docs.

- [x] Add real-profile assertion for RTC capability/config; observe failure, then
  enable only RTC on Plus. Confirm C3/ReadPico profiles and disabled RTC fallbacks.
- [x] Run focused/full host checks and native S31 build; verify startup/image/slot,
  archive a separate combined candidate, source hashes and exact limitations.
- [x] Independent review; fix material findings with regression checks.
- [x] Update device backlog and batch test instructions. Keep physical acceptance
  pending, accepted firmware unchanged, and modifications uncommitted.
