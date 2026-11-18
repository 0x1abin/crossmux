# Metalio Plus native sixteen-gray implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Expose real native sixteen-level targets while preserving accepted periodic cleaning.

**Architecture:** Borrow the existing vendor front plane in the Plus driver.
Pass request-local refresh mode through compatible facade/HAL/renderer extensions.
The reader publishes cadence only after successful native commits.

**Tech Stack:** Existing epdiy native buffers, C++ production-driver recording tests, renderer/reader host tests.

**Spec:** [Native gray design](../specs/2026-11-13-metalio-plus-native-gray-design.md).

## Global Constraints

- User order: IMU first, native16 second, cover/image integration third, build repair last.
- No flash, device/SD/settings mutations or physical acceptance while away; no implicit commit/push.
- Existing 415,872-byte native front plane only; no new full-screen allocation, task, dependency or vendor changes.
- Preserve other drivers and default native behavior, B/W proxy, cancellation and failure safety.
- Full/unknown GC16; ordinary native gray GL16, never DU; periodic/manual cleaning survives.

## Review Focus

- Cancellation or a different API must not promote uncommitted pixels to the back-plane baseline.
- Native16 and overlay uploads cannot be mixed or committed twice.
- Failed hardware work must not trigger an immediate cleanup/BW retry.
- Cadence debt and manual Full survive failed native commits; legacy drivers keep their old behavior.
- Packing, clipped/oriented proxy writes and two-bit-font fallbacks must preserve visible UI/text.

### Task 1: Native Plus driver transaction

**Files:** SDK MetalioPlusEpd.{h,cpp}, MetalioPlusDriver.{h,cpp}, BoardConfig/profile harness;
new native_cases.h in the existing production display harness.

**Interfaces:** `metalioPlusEpdStageNative()` borrows a white native front plane;
existing `beginGrayscale16/commitGrayscale16/cancelGrayscale16` implement a separate Native16 pass.

- [x] Extend production recording harness, watch begin return nullptr before implementing.
- [x] Add borrowed-white stage and native driver lifecycle; keep legacy mask calls intact.
- [x] Test sixteen literal levels, one activation, native-to-mask/BW transitions, cancellation,
  repeat commit, resync/sleep and each hardware failure. Expected: old/new driver suites pass.
- [x] Assert Plus profile16 first (RED), then change profile and verify C3/ReadPico unchanged.

### Task 2: Per-request mode and successful reader cadence

**Files:** PanelDriver.h, FreeInkDisplay.{h,cpp}, HalDisplay.{h,cpp}, GfxRenderer.{h,cpp},
ReaderRefresh.h, EPUB native-text branch, existing facade/reader host tests.

**Interfaces:** `supportsNativeGrayscaleRefreshCycle()` defaults false, Plus true;
`commitGrayscale16WithMode(bw,RefreshMode)` defaults to old driver commit;
facade/HAL/renderer add `commitGrayscale16(RefreshMode)` while retaining no-argument calls.
`ReaderUtils::commitNativeWithRefreshCycle(renderer,counter,manual)` returns success.

- [x] Add mode-dispatch/legacy-fallback and reader cadence/failure tests before implementation.
- [x] Add compatible extensions and Plus mode-aware commit, mapping non-Full native to GL16.
- [x] Integrate transactional helper for existing four-bit EPUB text; no immediate BW retry after failed Plus commit.
- [x] Run real facade/Plus/renderer/native-text/cadence suites. Expected: mode and physical-state tests pass;
  existing drivers retain byte-for-byte source and behavior.

### Task 3: Preserved software candidate and return checklist

**Files:** device guide/backlog/acceptance docs, SDK README/testing; external source snapshot and original fixture generator.

- [x] Run focused/new regressions and full host CTest; retain known baseline failures by name.
- [x] Prepare an original sixteen-tone fixture without uploading or modifying device data.
- [x] Preserve exact source/tests and refresh/resource contracts; physical16 acceptance remains pending.
- [x] Continue to the cover/image subsystem without repairing the native build environment early.

Commit steps await an explicit user request.
