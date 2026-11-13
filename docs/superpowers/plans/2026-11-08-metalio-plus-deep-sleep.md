# Metalio Plus deep sleep implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline in the existing
> isolated checkout. The user has repeatedly selected continuation; do not add
> another process approval. No commit, push, release or automatic restoration.

**Goal:** Wake the retained reader with the real TCA P17 -> GPIO2 power-key path.

**Architecture:** A native SDK board adapter prepares checked EXT1 sleep; HAL
calls it after display/storage shutdown. The persistent host updater preserves
the first original recovery record across subsequent debug images.

**Tech Stack:** Existing native Arduino/IDF6.1 S31 build, Python host contracts,
recording C++ vendor/IDF API harness and real ELF verification.

**Spec:** ../specs/2026-11-08-metalio-plus-deep-sleep-design.md

## Global constraints

- V1.4 S31, 16MiB Flash/PSRAM200MHz; app only 0x220000..0xd00000.
- Preserve frozen vendor files, SDK gitlink, bootloader/table/rescue/fonts/eFuses.
- Preserve active manifest and its original NVS/otadata/phy snapshots.
- Keep firmware/SD trial changes until explicit user restoration request.
- No new dependency installs; no allocation in new firmware power logic.

## Review focus

- Held key, bad I2C or IRQ stuck low must abort with no wake-less sleep.
- Partial mask/rail failures retain cleanup responsibility and return failure.
- Wake must not depend on direct input.power or on light-sleep PLL retention.
- Update failure must retain original recovery association and snapshots.
- Native tasks and DMA/filesystem work must finish before rail removal.

### Task 1: Checked board preparation and HAL routing

Files: new SDK MetalioPlusPower.h/.cpp and real-production host harness;
modify MetalioPlusEpd/library.json and root test/CMakeLists.txt;
modify lib/hal/HalPowerManager.cpp only for the Plus path.

- [x] RED: recording native tests for successful ordering, release wait, every
  fallible step and partial-change cleanup; compiled production object required.
- [x] Implement bool metalioPlusPrepareDeepSleep() with the exact spec sequence.
- [x] Route Plus HAL to adapter -> SDK deepSleep; false -> logged esp_restart.
- [x] GREEN: native power and existing Plus display/input/SD/profile regressions.

### Task 2: Persistent debug image update

Files: tools/reader_recovery.py, run_v14_reader.py and their existing tests in
independent development root.

- [x] RED: original snapshots and active marker unchanged after update/failure,
  explicit restore recovers original app/config; bad current/immutable rejected.
- [x] Implement update_debug(adapter,manifest_path,root,plan,update_dir,capture)
  with verified current candidate state and durable per-update journal before write.
- [x] Add explicit --update-and-keep MANIFEST with persistent-policy/SD/image gates.
- [x] GREEN: all independent reader/USB/SD/guard tool tests.

### Task 3: Build, review and device acceptance

- [x] Build S31 with existing tools/pio-s31-200m; archive image/ELF/map/checks.
- [x] Verify real HAL routing, image identity/size/hash, startup order, frozen
  vendor and SDK gitlink. Run required checks and report existing limitations.
- [x] Update retained reader with original recovery record and capture logs.
- [x] User-operated three sleep/wake cycles; check UI/touch/EPUB/keys/progress.
- [x] Record results, keep CrossMux and SD changes, leave current draw pending.

### Task 4: Correct wake rail dependency (observed SD 0x108)

- [x] Reproduce deep wake with exact reset=8/wake=3 and SD card-init 0x108.
- [x] Single-variable keep-rails experiment: user accepts reader, next page and
  Home after wake. USB debug requires a physical replug; early wake log missed.
- [x] RED/GREEN: 16 real early-rail adapter cases, preserving unrelated TCA output
  and direction bits and refusing failures; normal 17-case sleep suite restored.
- [x] Remove temporary keep-rails source; restore normal sleep rail cut.
- [x] Build/review early native rail restoration before SD, then archive/verify.
- [x] Run normal-cut firmware for three physical sleep/wake cycles.
- [x] Record USB reconnection limitation and leave current measurement pending.

## Completed bounded checkpoint

Three corrected-image manual cycles passed by user observation; one full
reset=8/wake=3/SD-ready trace was captured. Candidate and immutable regions
were verified afterward, without programming or restoration. USB enumeration
remains unreliable and is a separate open issue. Sleep current, battery-only
operation, CNNT electrical parking and endurance remain pending. Existing full
CI limitations remain: absent cached legacy platform, inconclusive cppcheck,
and six baseline host failures (1,528/1,534 passed). This plan does not claim
production readiness or power acceptance. Automatic sleep remains Never.

Evidence: parent logs/plus-early-rails-acceptance.json.
