# Metalio Plus runtime recovery implementation plan

## Current user instruction — 2026-11-08

The user returned and explicitly requested persistent debugging: keep CrossMux
and SD trial changes in place until they request restoration. This supersedes
the temporary automatic-restore sequence below and removes SD rollback as a
prerequisite to further tests. Use the explicit `--write-and-keep` mode with
`artifacts/metalio-v14-reader-debug-plan.json`. Preserve fresh mutable snapshots
and the active recovery manifest; a capture timeout or interrupt never restores
in this mode. The original automatic mode remains available for its historical
plans, but is rejected when the plan records `on-user-request`. No new approval
is needed for the already-authorized debugging.

> **For agentic workers:** Use superpowers:executing-plans inline in the existing
> isolated checkout. Preserve the user's no-commit/no-push scope.

**Goal:** Prepare a representative reader image and recoverable temporary run
without automatic NVS formatting or unbacked-up SD changes.

**Architecture:** A Plus-only NVS init wrapper contains Arduino's formatting
fallback. A separate host runner takes fresh mutable Flash backups and restores
the original app plus changed mutable partitions. SD backup is an independent
prerequisite, not something the Flash runner claims to cover.

**Tech stack:** Pinned Arduino4 RC1/IDF6.1 source build, Python3.14, esptool5.6,
existing isolated toolchains and standard-library host tests.

**Spec:** `../specs/2026-11-07-metalio-plus-runtime-recovery-design.md`.

## Global constraints

- Match the connected V1.4 S31, 16MiB Flash and 200MHz Octal PSRAM.
- Preserve bootloader, table, rescue, fonts and SDK gitlink; no eFuse writes.
- Fresh NVS/otadata/phy backup: 0x9000..0x12000; full app restore: 0x220000..0xd00000.
- Existing microSD must be backed up and verified before a full-reader trial.
- Keep programming guard enabled; no commit, push or release.

## Review focus

- NO_FREE_PAGES/NEW_VERSION_FOUND must not fall through to Arduino erase.
- Healthy/unrelated NVS errors must retain their actual result and single-call behavior.
- Bootloader and legacy targets must not inherit the application wrapper.
- Failed or interrupted app writes must still attempt restoration from fresh durable data.
- Wrong-device, truncated, stale or altered backup plans must fail before writes.

### Task1: Plus NVS error policy

Create `lib/hal/HalNvsPolicy.cpp` and a compiled-production host harness
`scripts/tests/test_metalio_plus_nvs.py`. Add the wrapper through the existing
app-only `scripts/metalio_plus_link.py` mechanism and register the host test in
`test/CMakeLists.txt`.

- [x] RED: actual wrapper returns safe init result for NO_FREE_PAGES and NEW_VERSION_FOUND;
  ESP_OK, NO_MEM and NOT_FOUND pass through; real init called once; non-Plus object
  exports no wrapper. Verify app wrapper is removed before IDF clones bootloader.
- [x] Implement `extern "C" esp_err_t __wrap_nvs_flash_init(void)` with no allocation.
- [x] GREEN: run compiled harness and existing native build/link script regressions.

### Task2: Fresh mutable Flash backup and restore contract

Create independent-root `tools/reader_recovery.py` (testable validation and
transaction orchestration), `tools/test_reader_recovery.py`, and
`tools/run_v14_reader.py` (explicit CLI and actual esptool adapter).

- [x] RED: validate exact non-overlapping regions, SHA/size/device/table/app
  prerequisites; reject altered/truncated snapshots before any write.
- [x] RED: injected probe/capture failure still restores app and changed mutable
  partitions; unchanged mutable partitions are not rewritten; restore errors are
  surfaced and durable manifests retained; immutable mismatch fails acceptance.
- [x] Implement fresh backup before app writes, bounded USB recovery using
  `probe_usb_session.py`, explicit prepare/check-plan/restore modes and secure files.
- [x] GREEN: host contract tests and existing USB/compile-guard regressions.

### Task3: SD backup and exact trial preparation

- [x] Resolve current card access with the user; no SD writes before backup.
- [x] Inventory/backup files including hidden paths and root guide/crash paths;
  verify source stability and copied hashes. Retain a restore manifest.
- [x] Record card identity and chosen bounded physical trial; keep SD restoration
  distinct from Flash acceptance. If physical access is unavailable, finish
  independent host/build work and report that exact prerequisite.

### Task4: Build, checks, review and handoff

- [x] Build S31 via existing `tools/pio-s31-200m`; preserve new artifact/map/hashes
  separately from historical build-only artifacts.
- [x] Verify app-only wrapper symbols/scopes, image/partition identity, code-format
  and whitespace, focused and required repository checks; report known failures.
- [x] Fresh review of changed policy/runner and one focused RED/GREEN repair pass
  for material findings. No firmware writes during review.
- [x] Prepare concrete reader run/restore plan with verified SD backup, then obtain
  any outstanding authorization for the additional mutable-region restoration.
- [x] Record current firmware, latest evidence and remaining physical acceptance.

## Execution ledger

- Ruling: reuse existing isolated branch and inline method; user continuation
  selects the previously proposed startup-write/recovery stage. No renewed
  installation, commit, push or generic programming permission is inferred.
- Ruling: fresh mutable backup plus guarded normal NVS is the smallest
  representative path; scratch NVS would conflict with current IDF safety rules.
- User selected current microSD with affected data backed up first. Access method
  is pending while independent policy/runner/build work proceeds.
- Task 1: complete. Production NVS harness 2/2 and focused CTest 11/11 passed;
  native image built and actual Arduino call sites verified. Logs are in the
  independent root under `crossmux-plus-nvs-*`.
- Task 2: complete as host implementation, not hardware acceptance. Recovery
  engine 14/14, CLI 4/4, existing USB 9/9 and programming guard 2/2 passed.
- Task 3: backup core and Mac-volume CLI implemented. Nine host tests pass;
  exact external physical disk, measured capacity, read-only state, UUID and
  stable file hashes are required. No external disk is currently visible.
  Actual selected-card backup, insertion sequencing and SD restoration remain
  pending the operator's access response. A file-tree copy is not a raw image.
- Ruling: use a complete file-tree backup if capacity permits, rather than infer
  a complete writable-path list from a few startup filenames — renamed staged
  files and user actions can touch other paths — cost is local space and copy time.
- Task 4: S31 build passed (6,586,928 bytes, SHA256
  `7b510781beda28beaeacfdf435d15c0b2afd490b1b7a5ec9261468a29dad55ec`).
  Full host suite 1525/1531; six known baseline portability failures remain.
  Full CI/default and legacy matrix not run: pinned platform 55.03.311 is not
  cached, and no unapproved dependency installation was attempted. Native full
  cppcheck is still inconclusive due to the previously documented cache prefixes.
- Final review: fresh read-only reviewer found four Important issues. One repair
  pass added tests and fixes: safe esptool logging after ENOSPC, UUID-associated
  active transactions blocking completed-backup replay, BaseException isolation
  in critical recovery progress output, and directory fsync before publication.
  Corresponding RED logs and GREEN suites are retained in the independent root;
  the final recovery/USB/SD/guard checks passed 38/38. No second review requested.
- Ruling: run the old guard harness as its own executable — it intentionally
  raises SystemExit at import and cannot be included in generic unittest
  discovery — cost is a separate test command. Generic discovery failure is
  retained in `logs/reader-recovery-all-host-tests.log`, not counted as passing.
- Ruling: preserve this uncommitted plan and local evidence instead of deleting
  scratch evidence or creating commits — no commit/push was requested and
  physical preparation is incomplete — cost is retaining development files.
- No new device access, firmware writes, mutable restore or SD writes occurred
  during this recovery-preparation increment. ThingsOS remains the last restored
  and physically accepted firmware. The reader plan check reports write blocked
  before adapter/device access because `sd_backup` is null.

### Card access and final preparation update

- The external USB card appeared while finishing checks: disk4/disk4s1,
  15,938,355,200 bytes, UUID `9562187A-9071-36F6-A5E0-787DA2A9F51A`, matching
  the measured card capacity. The partition was remounted read-only; no Flash
  or serial connection was made. Metadata and mount evidence are retained in
  parent `logs/sd-*-access-metadata.json` and `sd-card-readonly-remount.log`.
- Actual diskutil output uses `WholeDisk`, not the initially assumed `Whole`.
  Updated the fixture, observed the failure, corrected the key and reran GREEN.
  Evidence: `logs/sd-volume-diskutil-key-red.log` and final SD suite.
- Ruling: explicitly exclude only `.Spotlight-V100` from this backup — macOS
  rejects traversal of its index, while ordinary reader/user directories must
  still fail on unreadable data — cost is no saved Spotlight index, which is
  outside the reader-data rollback scope. The readable `.fseventsd` was backed
  up; Spotlight/FSEvents are both preserved during rollback.
- Task 3: complete. Private backup `backups/sd-card-20261107-234033/manifest.json`
  contains 441 files / 321,774,690 bytes and 54 included directories; source
  stability, copied bytes/hashes and before/after volume identity passed.
  Manifest SHA256 `cfef1ee2582e3c33f930c122977566a551b008b659789d2c56eb1b3e8e90bd2e`.
  The initial full reader-data comparison has zero differences.
- Added explicit reviewed SD rollback using installed native `/usr/bin/rsync`;
  tested actual add/change/delete/empty-directory recovery in private temporary
  directories. Identity, writable-state, altered-backup and stale-current-content
  gates pass. No actual-card rollback or SD file writes were performed.
- Ruling: gate reader writes separately on recorded authorization even after SD
  verification — the selected card covers backup-first SD use, but earlier
  diagnostics left mutable Flash unchanged and did not restore it — cost is one
  concrete confirmation of the additional mutable Flash scope before the trial.
- Final host-tool suites now pass 45/45 (recovery/CLI19, SD15, USB9, guard2).
  Reader plan contains the verified SD manifest, 600-second operator sequence,
  fresh mutable Flash recovery and explicit same-card SD rollback steps.
  `--check-plan` confirms image/SD integrity and blocks programming because
  `trial_authorized` is false. Remaining preparation item: obtain authorization,
  then take fresh live mutable Flash snapshots immediately before the run.
- After backup and zero-difference comparison, disk4s1 was safely unmounted.
  Evidence: `logs/sd-card-safe-unmount.json`. No SD file rollback or full-reader
  Flash transaction has been run. The operator must exit USB disk mode before
  the existing serial transport can reconnect for any approved trial.

### First complete-reader run and startup correction

- User authorized the 600-second trial and fresh mutable Flash/SD recovery, and
  confirmed operator presence. Fresh transaction `20261107-235746` matched the
  device/table/original app and backed up current NVS/otadata/phy. Candidate
  write/hash verification passed. No new authorization is needed to correct and
  retry within the same temporary-run/restore scope.
- The reader faulted before app_main in a global constructor. Matching ELF
  `a263d7aeaa1bc8727dc11d99201ea5b3c46db7e3681d0b58e71f7d8a68fbd669`
  and actual stack resolve UITheme -> logPrintf -> Print::write. HWCDCSerial is
  the receiver at 0x2f02c5a0; its zero vtable is dereferenced at 0x0c.
  Evidence: `UITheme.cpp:55-59,115`, `Logging.cpp:65-74`, decoded stack and
  disassembly in the parent first-trial analysis. The fault occurred before
  Arduino init/NVS or SD mounting; the repeated trace established reproducibility.
- Interrupted capture after sufficient repeated traces. The recovery engine
  restored/verified the complete app, verified unchanged mutable data without
  rewriting it, verified all immutable ranges and cleared the active marker.
  The user confirmed normal ThingsOS; same-card read-only comparison is unchanged.
- Ruling: select the already-supported ROM transport for Plus only at
  `BoardConfig.h:475-488` — native S31 USB Serial/JTAG is configured as the IDF
  console before constructors; `Logging.cpp` already supports ROM output — cost
  is using direct console output for Plus runtime logs, requiring device capture
  validation. Do not reorder shared UI globals or change legacy board transports.
- RED: production Logging.cpp harness accessed the unsafe Serial stub; Plus
  profile assertion also failed. GREEN after the selector change: ROM output,
  crash ring and no Serial access pass; C3/ReadPico policies unchanged. Existing
  serial transport, touch-profile and Paper Mono boot host suites also passed.
- Rebuild and focused checks in progress. First failed image/ELF, backup manifest,
  serial and restore reports are retained separately. No CI or physical reader
  success is inferred from the host regression.

### ROM-log trial, input correction and operator pause

- ROM-log image compiled and passed image/partition/wrapper checks plus 12/12
  focused host tests. The actual logger routes to esp_rom_printf. The operator
  returned from USB disk mode and confirmed readiness for the same-scope retry.
- Trial `20261108-001456` reached InxRecent, mounted SD, loaded fonts, and built
  an EPUB index/cover without panic. The user saw the interface but reported no
  tap/open response. Twenty-eight heap samples and nine display timing samples
  are recorded in `logs/reader-romlog-trial-runtime-analysis.json`. No reading
  pages or full input acceptance is claimed.
- Root cause: `HalGPIO.cpp:235-238` deferred input for Plus, but the only call
  after `display.begin` in `main.cpp:691-698` selected ReadPico alone. The log
  lacked "Starting input backends" and the real ROM-log ELF had no call to
  HalGPIO::beginInput. This is startup wiring, not a coordinate fit.
- Added `scripts/verify_metalio_plus_startup.py` to check compiled production
  call order rather than another isolated backend mock. RED rejected the actual
  ROM-log ELF (zero input-begin calls). One guard extension includes Plus;
  GREEN on the rebuilt ELF verifies exactly one display -> input -> renderer
  call sequence. Reproduce with the target objdump; host mocks alone do not
  validate this integration boundary.
- The user went to sleep and explicitly requested pause at manual feedback.
  Interrupted the active trial and restored the full ThingsOS app plus the fresh
  pre-trial NVS. Both verified; otadata/phy unchanged and immutable ranges matched.
  Active marker cleared. Latest physical ThingsOS acceptance and SD rollback are
  pending; no more device operations were attempted after Flash restoration.
- Ruling: finish independent source/test/build work, then hold physical work —
  manual USB mode is necessary for safe SD comparison/rollback, and actual touch
  feedback is necessary for the next trial — cost is waiting for operator return.
  Existing scope authorization is retained; temporary execution flag is disabled
  so the CLI cannot program while the user is away. Do not ask for the scope again.
- Corrected input-startup artifact built successfully: 6,588,864 bytes, SHA256
  `5a32cbfd9bdf5349e49259825b7cd4c0c13142480ddcb2e17696f57ff4f8252c`,
  static RAM107736, linked Flash6588006. It has not been flashed. Actual startup
  disassembly, ELF, map and checks are retained independently from prior images.
- Final full host suite: 1526/1532, with the same six baseline failures. Root
  formatting and changed SDK log-selector range pass; whole SDK header formatting
  is not claimed. Legacy firmware matrix/full CI remain pending as documented.

### Historical resume proposal (superseded by the user on 2026-11-08)

1. Keep ThingsOS; ask the user to open its USB disk mode before reading books.
   Flash recovery already completed: do not replay the completed trial's mutable
   snapshots after ThingsOS has resumed. Only SD rollback is pending now.
2. Identify the same card, mount read-only and compare against the private backup.
   Review expected trial paths and preserve unrelated later user changes. Apply
   only the reviewed same-card rollback, verify hashes/tree and unmount safely.
3. Confirm original operation/exit USB mode and operator availability. Restore the
   execution flag from the retained authorization; do not request permission again.
4. Use the checked input-startup image, fresh live mutable snapshots and the same
   bounded restore runner. Verify "Starting input backends" then physical taps,
   book opening, page turns and return navigation. New image not yet flashed.

### Current retained-debug checkpoint, 2026-11-08

- User explicitly requested continued debugging and no restoration until asked.
  Recorded persistent policy in `metalio-v14-reader-debug-plan.json`.
- Added explicit `--write-and-keep`, with CLI policy mismatch rejection before
  device access. Keep mode retains app/configuration and the active immutable
  recovery association after capture completion, interruption or failed writes;
  explicit restoration remains available. RED/GREEN plus 50 tool tests passed.
- Deployed and verified the corrected input-startup artifact with fresh mutable
  snapshots. Active manifest: `backups/reader-runs/20261108-163445/manifest.json`.
- User accepted book-cover touch/open, normal text/orientation, both volume
  page keys, cover previous/next/Home and repeated page turns.
- Stopped capture intentionally without restoring, verified the candidate app
  and protected Flash, then performed an ordinary reset with no Flash writes.
  Saved/reopened spine/offset/page matched; user accepted progress retention,
  further page turning and persisted Never sleep setting. USB capture: 180s,
  zero disconnect/reconnect/open failures. Runtime evidence is in parent
  `logs/reader-input-startup-debug-runtime-analysis.json`.
- Current next work: Plus board-owned power/sleep/wake, then remaining peripherals
  and refresh modes. Keep auto-sleep Never until wake integration is validated.
  Preserve the active original snapshots on future debug updates; do not prepare
  over them and do not automatically restore to permit a new build.
- Original Flash and SD backups are retained. No restoration, commit, push or
  release was performed in this retained-debug run. Known firmware CI/static
  analysis limitations remain as recorded above.
