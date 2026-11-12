# Metalio Plus Four-Level Grayscale Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable combined four-level grayscale and text AA without restoring a full flash on each page.

**Architecture:** Stage the base and ordered overlay rows into the existing vendor
4bpp front buffer. The Plus driver validates complete coverage and performs one
checked commit; the native back buffer continues to represent actual completed ink.

**Tech Stack:** C++20/17 host harness, native IDF6.1/Arduino, existing epdiy vendor source.

**Spec:** [four-gray design](../specs/2026-11-09-metalio-plus-four-gray-design.md).

## Global Constraints

- V1.4/S31 only; 1216×684, native even-pixel low nibble, levels 0/5/10/15.
- No driver allocation, new dependency, vendor blob/waveform/calibration changes.
- Preserve configured Full interval, default SDK gitlink and restore-only-on-request.
- User delegated details and requested continuous execution. Work in the existing
  named development branch. No implicit commits/pushes; earlier PRs remain Draft.

## Review Focus

- Cancelled/OOM/incomplete rows must not refresh obsolete content.
- Gray cleanup must preserve the committed gray back frame.
- Gray→BW and sleep/recovery must use a suitable waveform and correct baseline.
- Image pages must take the combined path even when strip uploads are available.
- Invalid uploads and failed power/updates must not report success or retry blindly.

### Task 1: Native staged commit and validated overlay driver

**Files:** SDK `MetalioPlusEpd.{h,cpp}`, `MetalioPlusDriver.{h,cpp}`,
`MetalioPlusEpd/test/host/{run.py,test.cpp}`.

**Interfaces:** Add `uint8_t* metalioPlusEpdStageBase(const uint8_t*)` and
`bool metalioPlusEpdCommit(MetalioPlusRefresh)`; keep B/W draw signature. Implement
the existing PanelDriver grayscale base/whole-plane/strip/display/cleanup hooks.

- [ ] Add real-driver cases: four gray values, no B/W prepass, ordered strips,
  whole-plane parity, bad coordinates/missing/invalid planes, cancellation,
  cleanup fallback, gray/BW transition, repeat/new begin, Full/sleep/resync and faults.
- [ ] Run existing harness; observe expected capability/behavior failure.
- [ ] Implement split staging/commit and enum-based pass ownership. Track coverage,
  refuse incomplete/invalid commit, and retain old power/baseline failure contract.
- [ ] Run the expanded harness: all new and previous fourteen cases pass.

### Task 2: Capability/profile and combined image routing

**Files:** SDK `BoardConfig.h`, its Plus profile test; CrossMux
`EpubReaderActivity.cpp`, reader cadence tests and device docs.

**Interfaces:** Overlay Combined strip capability from Task 1; no new HAL method.

- [ ] Add profile expectation `grayscaleLevels == 4` and coverage for combined
  gray base cadence on Plus, ReadPico and X3; observe profile test fail first.
- [ ] Enable four levels and admit explicitly Combined image stages in EPUB.
  Keep Full debt/manual refresh and all existing device fallback policies.
- [ ] Run profile, reader, grayscale renderer/HAL and affected legacy host checks.

### Task 3: Integration, native build and physical acceptance

**Files:** plan/spec/device checklist; external build, artifact and recovery records.

- [ ] Compare frozen vendor blobs, format only touched integration files, check
  diff whitespace; run required CI and full host suite, recording known failures.
- [ ] Build `metalio_eink4_plus` using isolated existing tools and a fresh
  `PLATFORMIO_BUILD_DIR`; verify native config, ELF startup and image/slot hashes.
- [ ] Independent whole-change review; resolve material findings with regression checks.
- [ ] Confirm device identity/current image and protected regions using existing
  reader update tooling. Deploy app-only under retained debugging authorization.
- [ ] Request concrete user checks from the spec; keep serial capture open for
  the requested interaction and record actual feedback. No automatic restoration.
- [ ] Update acceptance/status/docs; leave code uncommitted until requested.
