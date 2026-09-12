---
name: port-device-bsp
description: >-
  Bring new hardware into CrossMux through FreeInk SDK board profiles and drivers,
  CrossMux HAL integration, builds, and recorded physical acceptance. Use for new
  device onboarding, BSP porting, board adaptation, hardware bring-up, 新设备接入,
  BSP 移植, 板卡适配, or 硬件 bring-up. Not for ordinary UI changes or upstream sync.
---

# Port Device BSP

Guide a device from hardware evidence to a working reader with recorded build
and physical validation. Follow the user's language for questions and reports.
Accept incomplete inputs; discover facts before asking for missing decisions.

## Establish the hardware contract

Read [AGENTS.md](../../../AGENTS.md) and follow the
[repository workflow](../../../docs/engineering/git-workflow.md). Inspect the
worktree, SDK gitlink, SDK checkout and its own agent instructions. If the SDK is
missing, use the [development workflow](../../../docs/contributing/development-workflow.md)
to initialize the pinned submodule (`git submodule update --init --recursive`).
Preserve local SDK changes; do not use `--remote` or switch to the latest upstream.
If source remains unavailable, report that limitation and continue the hardware
inventory without inventing SDK APIs.

Collect the model and board revision, schematics or vendor examples, requested
features, and available hardware/serial access. Extract this compact inventory
from supplied material and current source, citing file/line or document evidence:

| Area | Facts to establish |
|---|---|
| Platform | SoC, toolchain support, Flash size/mode, PSRAM size/type, partition layout |
| Display | Panel and controller, native dimensions, orientation, bus/pins, BUSY polarity, reset and rail timing |
| Input | Physical buttons, touch controller, coordinates, interrupts, shared reset lines |
| Storage and buses | SPI or SDMMC wiring, bus ownership, I²C addresses, frequencies and pin conflicts |
| Power | Supply voltages, PMIC/rail controls, battery and charge sensing, shutdown and wake sources |
| Optional features | Requested RTC, frontlight, IMU, audio, USB or wireless capabilities |

Distinguish verified facts, assumptions and unknowns. For electrical parameters,
use the matching hardware revision's schematic/datasheet and reconcile conflicting
examples; existing firmware describes implementation, not proof of wiring.
Ask only for consequential gaps. Unknown voltage, pin assignments or power timing
block dependent initialization/flashing, not unrelated inspection and build work.

## Choose the smallest supported path

Search the pinned SDK's `BoardConfig`, display and hardware libraries for the
closest match, then inspect its actual callers in CrossMux. Cite the source paths
and lines that justify each proposed change. Do not choose a donor by SoC alone.

| Finding | Route |
|---|---|
| SDK already supports the board | Reuse its profile and drivers; add only missing CrossMux integration |
| New board with supported controllers | Add board configuration and reuse drivers; extend shared drivers only for demonstrated gaps |
| Unsupported controller | Add the required SDK driver and profile, exposing only capabilities needed by the target |
| Unsupported chip architecture | Assess toolchain, framework and SDK portability first; report platform work separately from BSP work |

Keep X3/X4 on one image. For any other runtime variant, establish compatible
silicon, partitions, initialization and reliable identification before sharing
an image. Otherwise use a separate hardware profile/build environment, as existing
S3 targets do. Read [device variants](../../../docs/engineering/device-variants.md)
and the relevant sections of [platformio.ini](../../../platformio.ini);
do not add every new board to the X3/X4 `DeviceType` enum.

Before changing a shared driver interface, inspect its callers and enumerate the
implementations that would need edits. Prefer board configuration or a compatible
extension that preserves existing entrypoints. Drivers with no required behavior
change should have zero diff against the starting SDK revision; justify exceptions
with a concrete need, not signature churn.

## Integrate through existing layers

- **SDK:** board wiring, controllers, drivers and hardware capabilities. Verify
  APIs against the pinned source. Reuse supported drivers before adding any library.
- **HAL:** expose the application access, lifecycle, synchronization and error
  handling needed by the board. Trace initialization through `src/main.cpp` and
  `lib/hal`; order it by real rail and shared-bus dependencies. Use the
  [architecture reference](../../../docs/engineering/architecture-and-patterns.md).
- **Application:** retain logical input and GUI/renderer interfaces. Use
  [UI and input](../../../docs/engineering/ui-and-input.md) for held-button release
  barriers, wake gestures, rotation and touch mapping. New device checks belong
  only where physical behavior requires them, not throughout reader activities.
- **Build:** follow the [build reference](../../../docs/engineering/build-system.md)
  to select the board, device/capability flags, memory mode and partition table.
  Check profile inheritance: PSRAM/BLE configuration and USB-capable prebuilt cores
  are not interchangeable with the tuned C3 core. Enable only supported capabilities.

Bring up serial diagnostics, display, input, storage and reading incrementally,
respecting their power prerequisites; then validate sleep/wake and selected optional
peripherals. Check shared buses/reset lines, display orientation, touch calibration
and one-gesture/one-action behavior. Do not copy panel waveforms, rail timing or
calibration values merely because two boards share a controller. Keep calibration
in the appropriate board configuration unless a user-facing choice is required.

When changing refresh state, distinguish controller RAM baseline validity,
physical grayscale cleanup still needed, and controller power state. RAM writes
alone do not prove a physical clean. Carry per-request intent with the request
rather than a persistent permission for the next call. Commit completion-dependent
state only after BUSY completion; on timeout, retain unknown state and avoid
subsequent RAM or sleep commands that require an idle controller.

Account for actual framebuffer stride, dimensions and buffer count, driver scratch
space, tasks and peak allocations. Separate internal RAM from PSRAM and verify
Flash/app-slot headroom against the selected partitions. Shared code must fit the
C3 baseline; PSRAM capacity does not establish internal/DMA allocation headroom.
Use [hardware constraints](../../../docs/engineering/hardware-constraints.md) and
[memory rules](../../../docs/engineering/memory-and-allocation.md) when budgeting
or allocating; label estimates separately from measurements.

## Build and verify on hardware

Use [testing and debugging](../../../docs/engineering/testing-and-debugging.md)
for required checks. From the repository root, build the selected environment with
`pio run -e <env>` and run focused checks for changed logic plus the required
repository checks (`./bin/ci-check` for code changes). New targets must be built
explicitly if existing scripts do not include them. Shared SDK/HAL changes require
existing-device regression coverage, including the shared X3/X4 image; record
which targets were checked. Record compilation, hosted CI, flash/hash verification,
runtime logs and physical acceptance separately; none substitutes for the next.
Simulator results do not establish physical acceptance.

For display logic changes, compile the real driver and relevant reader helpers in
focused host checks; use a recording/mock bus for command traces instead of
extracting production code with source-string slicing. Cover affected synchronous,
asynchronous, window and fallback paths, including BUSY timeouts. Distinguish text
antialiasing from grayscale images: test consecutive reading pages, the user's
periodic cleaning cadence, explicit FULL, grayscale-to-grayscale and grayscale-to-
menu transitions. A single successful draw does not establish correct reading
behavior. The [Metalio case](../../../docs/engineering/metalio-eink4.md) records
these regressions and their resolution; its commands, black-flash count and
acceptance cycle counts are board-specific examples, not defaults for other panels.

Before writing firmware, match the device, image, partition offsets and recovery
method. Before first overwriting factory firmware, record the user's confirmation
of an existing backup or prepare and verify a recoverable backup. If the user has
confirmed backup and authorized overwrite, do not require another backup. Derive
sizes and offsets from this board, not another board's example.
Flash only within the current task's authorization. Reuse existing authorization
without asking again. Serial capture can use
`python3 scripts/debugging_monitor.py <port>` from the repository root.

Record physical checks for the actual board revision and firmware/SDK revisions:

- Boot without panic/OOM; confirm detected identity and initialized memory/devices.
- Display supported full/fast/partial/grayscale modes, orientation and ghosting;
  exercise input during refresh and check touch edges and rotated coordinates.
- Exercise short/held buttons, navigation and popup transitions; the boot/wake
  gesture and a long-press release must not trigger an additional action.
- Mount SD, read/write files, open an EPUB and turn pages; confirm saved progress
  and settings survive reboot. Check controlled behavior with missing/unreadable SD.
- Verify battery/charge readings, USB versus battery operation, shutdown and at
  least three sleep/wake cycles; confirm display, input and storage recover.
- Test selected optional peripherals using the closest relevant device document,
  for example [Murphy M4](../../../docs/engineering/murphy-m4.md) or
  [Waveshare 3.97](../../../docs/engineering/waveshare-epaper-397.md). Adapt to this
  board's contract instead of inheriting all donor features or thresholds.

Capture free heap, historical minimum and largest free block before/after
initialization and repeated reading, peripheral and sleep cycles; record PSRAM
separately where present and investigate continuing decline. Power claims require
measured current and test conditions. Without hardware, finish possible builds
and supply a pending checklist; never report physical acceptance as passed.

## Handoff and optional extensions

### Keep the developer in the loop

Inspect supplied material first, then ask one to three questions needed for the
current stage: board revision and evidence, intended features, build environment,
or hardware/serial access. Reuse previous answers. Resolve conflicting evidence
with the developer before dependent work; continue independent work while waiting.
For visual checks, physical input or current measurements, give concrete steps
and wait for actual observations. Do not invent results from a successful build.

At each stage, briefly report results, blockers and the next action. Maintain the
target's engineering document throughout the task with hardware facts, decisions,
firmware/SDK revisions, reproduction commands and acceptance status. On resuming,
read that record and inspect current source and hardware revisions before repeating
checks or asking questions. Revalidate evidence affected by subsequent changes.

After the tested approach is selected, remove superseded experiment flags,
one-shot permissions and duplicate decisions while preserving needed board-level
calibration. Review the final SDK diff and CrossMux gitlink, reconcile current
behavior and pending acceptance in the device document, and update both README
language versions for new device support. Preserve historical evidence as history,
not as competing instructions for the current implementation.

Keep the handoff concise and use this structure:

1. Hardware facts and sources, including revision and unresolved assumptions.
2. Selected route and SDK/HAL/build changes with source evidence.
3. Verification table: check, target/revision, status, command/log/measurement.
   Use **passed / failed / pending / not applicable** (localized for the user);
   give a reason for not applicable. A missing measurement is pending.
4. Remaining blockers and the concrete next action needed for each.

### When the user requests a pull request

A request to take the port through PR submission includes preparing commits and
publishing the required contribution branches. Apply this section only for that
scope; an ordinary port does not imply a push. Follow the repository workflow,
create a focused `codex/` branch, and target `0x1abin/crossmux:main` unless the user
specifies otherwise. Inspect existing changes and remotes before acting.

- Check GitHub authentication and repository permissions. Use the authorized
  maintainer remote or create/use the developer's personal fork as needed; preserve
  unrelated remote configuration. If login is missing, guide local authentication
  and wait for it before publishing. Never ask for tokens in the conversation;
  finish independent implementation and PR preparation while authentication is pending.
- Review the final diff and fill the current PR template truthfully, including
  AI usage, resource impact, commands, results and limitations. Never mark a
  maintainer-coordination or hardware-test checkbox complete without evidence.
- Open a ready PR only when the build, required checks, physical acceptance and
  dependency integration pass for the submitted revision. Otherwise open a Draft
  with failed/pending checks and concrete next steps. Missing GitHub access remains
  a publication blocker, not a reason to claim a PR exists.
- If SDK changes are needed, prepare a separate SDK contribution under its own
  rules. Resolve the CrossMux SDK fork and base from current configuration and
  source, rather than assuming the upstream SDK is the contribution target.
  Cross-link both PRs. While the SDK PR is unmerged, keep the CrossMux PR Draft
  and document the SDK branch, SHA and exact local reproduction steps. Keep the
  existing committed gitlink; do not pin a commit available only in a personal fork
  as the default dependency. Report tests against the temporary SDK checkout as such.
- After the SDK is merged, pin the integrated commit available from the configured
  SDK remote. Compare its source tree with the tested SDK revision and inspect any
  build-relevant metadata differences; choose rebuilds and acceptance checks based
  on actual changes. A changed commit SHA alone does not require every build or
  flash to be repeated. Record the comparison and keep previously untested hardware
  items pending before judging readiness. If the dependency is still pending,
  return both PR links and the remaining
  action; do not merge it automatically or poll indefinitely.

Return the PR link(s), verification summary and remaining actions. Do not merge
PRs or publish firmware as part of contribution submission.

Build/CI integration does not automatically enroll a target in public releases.
For requested Nightly/OTA/Web integration, read
[firmware releases](../../../docs/engineering/firmware-release.md) and inspect
`scripts/nightly_targets.py` plus its consumers before changing release mappings.
Check model, board tag, slug, build environment, channel support, full-install
assets and indexes against consumer registrations, including Web when applicable.
Verify legacy language pointers resolve the unified image where that is the
release contract, and retain wrong-board and malformed-package rejection.
Check whether existing consumers accept a new target before choosing rollout
order; if they reject unknown targets, deploy compatible consumers before
publishing the new index. Discovering a Web dependency does not authorize edits
to that repository or deployment: report required follow-up outside the user's
scope and finish the authorized work.
For requested simulator support, read the simulator sections of the build and
device references and inspect the pinned simulator integration. Neither optional
branch replaces hardware acceptance. This workflow does not imply commits, pushes
or upstream synchronization; follow the current task's explicit scope.
