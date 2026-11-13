# Metalio Plus 200MHz Core Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline in the existing isolated checkout. Do not commit. Preserve previous local SDK changes.

**Goal:** Produce and verify the complete CrossMux S31 application with an actual 200MHz rebuilt core, retaining programming protection and specifying peripheral ownership.

**Architecture:** Use the pinned RC1 Arduino-as-IDF-component source build in a separate local package cache (see architecture ruling below). Adapt only demonstrated HAL drift outside frozen vendor files. Reuse the vendor native I2C owner for future input clients.

**Tech Stack:** pioarduino 61.04.00-RC1, Arduino 4.0.0-RC1, local ESP-IDF 6.1.0, GCC 15.2, Python host checks.

**Spec:** `docs/superpowers/specs/2026-11-07-metalio-plus-200m-design.md`.

## Global constraints

- All files/packages under the existing independent development directory.
- No device access, commit, push or release. Upload guard remains enabled.
- Preserve ThingsOS layout and every frozen vendor blob; existing boards retain their configuration.
- 200MHz octal PSRAM, DIO/80MHz 16MiB Flash, 64KiB internal reserve and mandatory PSRAM.
- Previous toolchain/dependency authorization covers rebuilding existing sources and required toolchain components in this directory; ask if an unrelated new prerequisite is needed.

## Review focus

1. A header-only clock change must not masquerade as a rebuilt PSRAM archive.
2. Core cache isolation must survive PlatformIO's framework restoration/child run.
3. HAL declarations must match the actual compiled core, not a version string.
4. Build success must not enable programming or imply input/storage acceptance.
5. Peripheral startup must not duplicate the existing native I2C owner or toggle shared reset lines blindly.

## Task 1: Rebuild and verify the memory core

**Files:** `platformio.ini`, new `config/metalio_plus_sdkconfig.defaults`, new `scripts/verify_metalio_plus_core.py`, new `scripts/tests/test_metalio_plus_core.py`; parent local `tools/pio-s31-200m` and isolated cache.

**Interfaces:** `validate_config(text: str) -> list[str]` returns contract violations; CLI accepts a generated sdkconfig and returns nonzero for violations. Core artifacts must be recorded separately from application image evidence.

- [x] Write and run failing tests for the missing verifier: accept 200MHz/DIO/16MiB/64KiB; reject 250MHz, QIO, wrong Flash, absent reserve and ignored missing PSRAM.
- [x] Implement parser/validator; run tests to green (final 6/6, including partition identity; logs/core-contract red/green).
- [x] Clone only mutable Arduino packages into a separate .platformio cache; reuse installed tools read-only where possible.
- [x] Add Plus-only custom-sdkconfig and local IDF package override. Build the full target, recording actual failures and fixes.
- [x] Validate generated core config and archives, image, partitions and guard; record hashes and resource sizes.

## Task 2: Reconcile HAL and peripheral requirements

**Files:** SDK `MetalioPlusEpd/src/lcd_idf_compat.c` and `LcdClockCompat.h` after demonstrated signature drift; reader `docs/engineering/metalio-eink4-plus.md`, `metalio-plus-peripherals.md` and parent session state/report.

**Interfaces:** preserve PanelDriver and native vendor APIs; publish a precise bus/reset/input/SD contract for the next hardware increment.

- [x] Diagnose actual HAL compiler errors; compile the production adapter against rebuilt headers after any fix. Retain old core compatibility if practical.
- [x] Trace native I2C owner, FT6336U coordinate/key handling, TCA reset/power and SDMMC slot support through SDK/HAL startup; document exact integration seams and blockers.
- [x] Run relevant SDK/reader host checks and format/diff/static checks, preserving baseline failures. Static analysis is not passing; see final ledger.
- [x] Fresh review of this increment; fix important findings with a failing regression then green check. Finalize evidence without flashing.

## Execution ledger

- Pre-flight: Task 2 consumes Task 1's generated HAL/config; inspect the rebuilt header before choosing an adapter. Neither task enables input or upload.
- Ruling: user continuation selects inline implementation of the previously described next step; no additional design/commit approval round. Cost if wrong: reversible local code/cache changes only.
- Ruling: retain this plan and logs because work remains uncommitted; scratch cleanup must not remove the only evidence.

- Ruling: use `framework = arduino, espidf` and explicit CMake defaults rather than RC1 HybridCompile. Its copy-back code refreshes archives/sdkconfig but not the IDF headers; native IDF6.1 LCD signatures differ from RC1. A single source/header graph avoids an unverified ABI mixture. Keep the isolated cache and programming guard. Cost: more initial compilation and minimal S31-only CMake entrypoints.
- Task 1 investigation: first attempt failed before CMake because the local ROM-ELF URL overrode the platform-managed installed-tool identity; remove that redundant local override. The next CMake run reached managed dependencies and was deliberately cancelled after finding the header/archive mismatch, before any core libraries were copied back.

- Task 1: generated sdkconfig passes the 200MHz contract; config verifier 5/5, version-path regression 2/2, existing BLE config 4/4 and programming guard 3/3 pass. Full source compilation is running.
- Ruling: native IDF creates application objects before the later source middleware pass. Define CROSSPOINT_VERSION before native environments are cloned; retain existing per-file middleware for Arduino-only targets. Root cause was confirmed by a local diagnostic showing git_branch add_version returning NodeList to the IDF File-only skip callback. The diagnostic was removed. Cost: native component builds recompile more files when Git version changes.

- Task 1: all application/SDK/IDF sources compiled, including the LCD adapter. Bootloader link exposed app build_unflags stripping its mandatory -fno-lto: bootloader_start.c.o has LTO sections while its link disables LTO. Remove that Plus-only unflag for the native build; preserve IDF bootloader non-LTO compilation.
- Ruling: materialize the ignored root CMake file from a tracked S31 template rather than tracking CMakeLists.txt itself. Older C3 HybridCompile deletes that root file unconditionally; a tracked copy would break existing builds. Owned-file/idempotency/unknown-file tests passed 3/3.
- Ruling: reset only this increment's generated sdkconfig once (preserved in parent logs) to apply final custom partition and size defaults. IDF retains previous menuconfig choices over edited defaults; the final generated values must be checked, not assumed.

- Final review: 2 Important findings. S31 root CMake also affected the next C3/S3 build before legacy cleanup; a real CMake-executed transition test failed on C3, then passed for S31/C3/S3 after guarding defaults/project selection by IDF_TARGET (4/4 owner tests).
- Final review: retaining -fno-lto in unflags was insufficient: native bootloader compile metadata does not contain that flag at all. Global -flto=auto still created LTO boot objects against a non-LTO link. Remove Plus-only LTO for this milestone, retain size optimization and measure resulting app headroom. Other targets keep their existing LTO settings. Hardware performance comparison is not claimed.
- Final: Ruling: retain build-only protection after matching 200MHz. Arduino NVS auto-erase and input/SD ownership/calibration still require explicit engineering before a full reader run; the old native diagnostic acceptance is not CrossMux acceptance.

- Ruling: application-only link hooks also need removal before native framework setup. PlatformIO applies BUILD_UNFLAGS after BuildFrameworks(), so that option was too late for the bootloader clone. The pre script now removes only the exact reader wrap flag from BUILD_FLAGS (quoting preserved); the post script adds it to the app after native environments exist. Pure filter regression failed then passed; final ELF checks confirm all three wrappers in the app, none in the bootloader, and the boot entry is defined. Boot startup object has zero LTO sections.
- Final build: full native Arduino/IDF target passed. Application is 6,540,320 bytes, linked Flash 57.3% of the existing 0xae0000-byte slot, static RAM 107,128 bytes. Generated config passes 200MHz/OCT/DIO/80MHz/16MiB/reserve/partition checks; native PSRAM archive is linked without prebuilt Arduino IDF archives. Image checks and partition-byte comparison passed.
- Final verification: focused CTest 9/9; repository suite 1522/1528, with the same six Apple-clang fixture failures reproduced in the unmodified export. Reader and new SDK adapter formatting plus whitespace checks passed. Real erase/upload invocation is rejected before device access. No hardware access or writes in this increment.
- Static analysis limitation: native strict cppcheck reports 91 high/4 medium/12 low IDF-header findings and aborts preprocessing. The package-prefix filter omits the new-cache Arduino includes but retains 249 IDF directories resolved into the reused older cache, leaving an incomplete include graph. This is an invalid/inconclusive full analysis, not a passing result. The earlier prebuilt baseline's FootnoteEntry diagnostic remains separately recorded; no suppressions were added.
- Static root-cause confirmation: exact checker options/source on `src/main.cpp`, changing only include-path scope, return exit 3 with one FreeRTOS preprocessor error for the partial graph and exit 0 with both framework package roots consistently excluded. This sample diagnoses the environment filter; it is not a complete static-analysis pass or an implementation fix.
- Restoration preparation: the new application exceeds the short ThingsOS image by 17,248 bytes. A local, permission-600 copy of the entire 0xae0000-byte original application slot was extracted and hashed from the full backup; it has not been written to the device. NVS startup behavior still requires review before a reader runtime plan.
