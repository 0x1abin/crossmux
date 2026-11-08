# Metalio Plus S31 build integration

Spec: `docs/superpowers/specs/2026-11-07-metalio-plus-build-design.md`.
Execute inline in this independent checkout on `codex/metalio-eink4-plus-s31`. User requested continuation; do not add another approval round for these reversible local changes. Do not commit.

## Global constraints

Use the pinned source versions and ThingsOS partition layout. Existing environments retain their platform and flags. New target is compile-only, with 250MHz prebuilt PSRAM explicitly unsuitable for flashing. Initial capabilities: synchronous B/W display only. No device access or release registration.

## Review focus

1. Pixel polarity/last-row stride: assert actual expanded bytes at both ends.
2. Failed clear/draw must not establish display history: inject each vendor return failure.
3. Failed power-off must stop subsequent panel commands: inject stuck PWR_GOOD.
4. Partial and asynchronous requests must take synchronous full fallback: exercise inherited entrypoints.
5. New board identity must not change C3/S3 selection or reserve internal framebuffer memory: compile actual profiles and run existing display harnesses.

## Tasks

1. Initialize exact SDK submodules, read their instructions, create compile-first host acceptance tests. Observe failure before implementation.
2. Add S31/Plus SDK profile and dedicated `MetalioPlusEpd` library plus `MetalioPlusDriver`. Vendor sources remain blob-identical. Native wrapper converts 1bpp into the existing 4bpp buffer and performs checked power/clear/update/power-off. Compile tests against the real wrapper and driver.
3. Add `metalio_eink4_plus` environment with existing S31 toolchain, matching partition CSV, new library dependency and upload guard. Local package URLs stay in ignored local config. Attempt full firmware build; diagnose failures from actual compiler output.
4. Run the new host harness and affected existing SDK harnesses, actual profile matrix, whole-project host checks and `bin/ci-check` when prerequisites permit. Record build/resource results and remaining runtime work in the engineering guide and parent session state. Review final diff; do not flash or commit.

## File map

- SDK `BoardConfig/include/BoardConfig.h`: S31 identity, conservative first-stage profile and PSRAM framebuffer sizing.
- SDK `MetalioPlusEpd/{include,src,library.json}`: frozen vendor backend and checked adapter.
- SDK `FreeInkDisplay/src/driver/MetalioPlusDriver.{h,cpp}`: PanelDriver integration, no new framebuffer.
- SDK `FreeInkDisplay/src/FreeInkDisplay.cpp`: gated backend selection.
- SDK `MetalioPlusEpd/test/host`: behavioral adapter tests using vendor API recording.
- Reader `platformio.ini`, `partitions_metalio_plus.csv`, `scripts/metalio_plus_build_only.py`: isolated target and upload block.
- Reader `docs/engineering/metalio-eink4-plus.md`: reproduction and validation limits.

## Execution ledger

- Source checkout and SDK pins verified; vendor standalone frame physically accepted before this stage.
- Plan ruling: use the existing explicitly requested independent development directory; no second checkout/worktree needed. User continuation authorizes implementation of the stated S31/display step. No commits.
- Plan ruling: keep the initial integration build-only because Arduino's prebuilt PSRAM is 250MHz and Wire ownership is not integrated. Costs: no CrossMux runtime claim from this stage; native 200MHz frame evidence remains valid.

- Task 1: complete — exact submodules checked out; adapter/profile/guard/tag/PSA tests observed failing before implementation, then passing.
- Task 2: complete — conservative S31 profile, checked native adapter, PanelDriver selection and 74 blob-identical vendor files added.
- Task 3: complete — full S31 firmware build/link passed; application 5,953,488 bytes, slot 11,403,264 bytes, DIO header. No hardware write.
- Task 4: complete for the build-only scope — final formatting and diff checks passed; the real PlatformIO erase_upload command was rejected before any device access. Regression, baseline comparison, review and artifact evidence are recorded in the engineering guide and parent session state. Whole CI and physical reader acceptance remain incomplete as documented below.
- Ruling: RC1 ships only qio_opi sdkconfig and QIO bootloader ELF. Select qio_opi plus boot=qio for build scaffolding, retain DIO image header, reject all programming targets. The generated bootloader/factory image is unusable for this device's installation.
- Ruling: vendor LCD HAL API drift is handled by a separate signature-checked C TU; original vendor blobs are unchanged.
- Ruling: IDF6 RSA key generation uses its public PSA export API, retaining existing DER wrappers; old framework path unchanged. SHA legacy symbols exist but need explicitly scoped private declarations. Crypto hardware acceptance remains pending.
- Ruling: GCC15/C23 breaks the pinned QRCode C header. Plus-only pre-build middleware applies C17 in actual C compilation environments. Changing only outer post-build env was insufficient and was replaced.
- Ruling: S31 omits the global GPIO hold API and only links manual GPIO isolation for two Kconfig settings. Match those declarations/source conditions; retain old-board behavior. Physical sleep/current remains pending.
- Review: independent reviewer found an erase_upload/__debug guard bypass. Added regression tests, changed to safe-target allowlist, then reviewed clean. Further adapter/HAL/PSA/C17/identity deltas reviewed without outstanding findings.
- Whole host CTest: 1518/1524 pass; six Apple clang fixture failures also reproduced in pinned unmodified source exports. ThemeMenus passed on rerun; its git-archive baseline was invalid because it requires Git history. Old ReadPico transaction harness fails Mac ld --wrap=calloc.
- Final focused CTest: 5/5 pass. Strict target cppcheck retains one low-priority portability diagnostic in unchanged FootnoteEntry.h:94, reproduced on the pinned baseline; it is not suppressed or counted as passing. The existing C3/S3 firmware CI matrix remains unexecuted.
