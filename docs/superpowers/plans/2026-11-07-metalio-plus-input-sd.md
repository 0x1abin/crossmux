# Metalio Plus native input and SD plan

Execute inline with superpowers:executing-plans; no commits or device access.
Spec: docs/superpowers/specs/2026-11-07-metalio-plus-input-sd-design.md.

## Constraints

Keep the existing isolated checkout and cache. Do not modify frozen vendor files,
release mappings, existing board behavior or global packages. Runtime remains
ineligible pending calibration, startup write containment and restore review.

## Task 1: Native Plus input

Files: MetalioPlusEpd/include/MetalioPlusIo.h, src/MetalioPlusIo.cpp and library.json;
InputManager include/src; BoardConfig.h; HalGPIO.cpp; production host harnesses.
Interface: plus I/O facade reads raw frames / expander port with checked status;
InputManager owns event state, strip gesture origin, failed-sample cancellation.

- [x] RED: compile/run real input/facade against controlled native boundaries;
  initially fail because the Plus profile has no touch/SD and native IO is absent.
- [x] GREEN: add locked IO, one-time reset and Plus-only input polling. Touch profile
  registration follows SDK checklist, held keys use existing debounce machinery.
- [x] Verify screen edges/move/lift, stale release without tap, strip entry isolation,
  home short/long, two contacts, failed bus/port and physical-button release.

Expected: host harness passes without any Wire begin/transaction or per-poll reset.

## Task 2: S31 SDMMC

Files: SdmmcBlockDevice.cpp; BoardConfig.h; Plus platform flags; SD host harness.
Interface: existing SdmmcBlockDevice API, same 4KiB buffer, native slot0 and cache
alignment callback. Task1 and Task2 share profile selection; no shared transport.

- [x] RED: production S31 block-device harness checks aligned internal DMA allocation,
  callback, slot0/pins/4-bit, failed init/allocation/read cleanup and chunked reads.
- [x] GREEN: S31-only initialization and buffer path; preserve legacy S3 path.
- [x] Run S31+S3 harness, affected SDK and reader host suites, full source build,
  formatting, partition/core/image/guard checks. Preserve existing failed checks.

Expected: valid source build, no changed vendor blobs, no device access.

## Task 3: Review and handoff

- [x] One fresh read-only review of this increment; important fixes RED->GREEN.
- [x] Update engineering guide, hashes, state and verification report truthfully;
  physical calibration, NVS and runtime remain pending, upload stays blocked.

## Ledger

- Ruling: user continuation selects the native peripheral step already proposed;
  proceed inline without another design confirmation. Cost: reversible local code.
- Pre-flight: Task1/Task2 share BoardConfig; tests must agree on provisional enabled
  profile and build-only status. Task2 consumes no I2C state and must not initialize
  vendor framebuffers. Keep input deferred after display; no early button polls.

- Task 1: production input/facade harness passed after absent source/profile RED.
  Added activity-transition regression: retained capacitive mask failed after
  suppressTouchContact, then passed after gating the getter on suppression.
  Native lock pairing and failure release verified without Wire or reset retries.
- Task 2: S31 allocator-contract regression failed on the old malloc path, then
  S31 and legacy S3 production block-device harnesses passed. Reuse one aligned
  4KiB buffer, preserve the legacy allocation/API shape.
- Validation: affected SDK suites 13/14 pass. Metalio AudioManager's existing
  speaker ownership assertion also fails in the unmodified SDK archive; unrelated
  audio source was not modified. Whole reader host suite 1524/1530 passes with
  the same six known Apple-clang fixture failures. Full native build in progress.
- Environment: initial build stopped in existing Python dependency setup; a
  verbose retry progressed into source compilation. No new manual installation
  or global environment change was made.
- Final review: no Critical, two Important cancellation findings, no Minor.
  Region crossing / failed strip reads produced actionable button release edges;
  failed screen reads hid the raw release needed by the UI drag cleanup latch.
  Both reports were reproduced in new regression modes before their fixes.
- Final: fixed canceled strip navigation — discard canceled virtual bits before
  ordinary debounce while preserving independent physical keys. Screen cleanup
  uses an explicit raw cancellation-release edge; tap/swipe remain suppressed.
  Regression includes real physical Up held across strip cancellation and its
  later legitimate release, plus expander-fault cancellation.
- Ruling: propagate checked TCA read status through the Plus physical backend,
  rather than flatten it into the status-free ButtonHook return value. This
  distinguishes a real release from an I2C fault, preserves the common debounce
  machinery and affects Plus only. Cost: a Plus-only physical backend branch.
- Final: forced FREEINK_CAP_TOUCH=0 compile regression exposed input state fields
  gated with the controller driver. Move Plus physical/cancellation state to the
  device guard; disabled-touch production compilation now passes.
- Final review declined physical calibration, key polarity/BOOT behavior,
  electrical/cache-coherency SD acceptance, sleep, startup writes and restoration.
  These remain pending and keep programming blocked; no runtime claim is made.

- Final validation: complete source build and deliberate relink passed. The final
  image is 6,586,768 bytes, linked Flash 57.8%, static RAM 107,640 bytes. Exact
  image/core/config/map digests, linked native input/SD symbols, wrapper scope,
  the unique board tag and partition identity passed local verification.
- Final validation: focused 11/11; full reader 1524/1530, same six named baseline
  failures. New Plus input (including forced touch-off) and S31/S3 block tests
  pass. Final raw-format and whitespace checks pass. Guard CLI rejects
  erase_upload before opening the deliberately nonexistent port.
- Validation limit: native cppcheck's previously demonstrated incomplete package
  include graph remains unresolved; no valid complete analysis of the final
  source is claimed. C3/S3 firmware matrix and hardware acceptance remain pending.
- Evidence preservation: an incremental CMake compiler probe overwrote the
  firmware.map path without relinking the unchanged ELF. Preserve the previous
  generated ELF and deliberately relink; the actual 28MiB application map now
  verifies native archive provenance and is copied into the parent logs. This
  does not modify source or access the device.
- No commit/push/release/device access occurred. Operator-presence question is
  pending; no physical observation or approval is inferred from elapsed time.
