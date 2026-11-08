# Metalio Plus startup and temporary reader recovery

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

The user asked to continue from the accepted native peripheral diagnostic and
selected the existing microSD card with affected data backed up first. Continue
in the existing isolated checkout and inline workflow. This document records
routine implementation decisions within that next step; it does not authorize
an unreviewed full-reader write or an SD modification before backup.

## Evidence and constraints

- Arduino RC1 `esp32-hal-misc.c:335-351` initializes NVS before reader setup and
  erases the first NVS partition on NO_FREE_PAGES or NEW_VERSION_FOUND.
- Reader `src/main.cpp:981-1041` mounts SD, loads/migrates settings and calls the
  user-guide preparation path. `lib/Serialization/PersistableStore.cpp:11-32`
  may rename staged files even from a read path.
- `src/util/UserGuide.cpp:17-20,41-103` writes hidden state plus a root EPUB;
  `lib/hal/HalSystem.cpp:123-140` can replace `/crash_report.txt` after a panic.
  Settings and reading caches use `/.crosspoint`, while explicit reading actions
  can affect `/read`, screenshots and other user-selected destinations.
- Board layout: NVS 0x9000/0x6000, otadata 0xf000/0x2000, phy 0x11000/0x1000;
  ota_0 0x220000/0xae0000. Bootloader, table, rescue and fonts are preserved.
- Initial backup rescue checksum is already invalid. Proven recovery is USB
  ROM/stub plus the original complete app-slot image, not rescue execution.
- IDF `esp_partition/partition_target.c:227-243` rejects writes into the running
  application partition. An invented NVS partition inside its unused tail is
  unsuitable with the current table and dangerous-write policy.

## Approaches and decision

1. **Selected engineering approach:** preserve ordinary NVS behavior, prevent
   Arduino's destructive fallback on Plus, and take fresh mutable-region backups
   immediately before a temporary reader run. Restore the full app slot and any
   changed mutable sectors, then verify them and all immutable ranges. This
   retains a representative reader without changing the device partition table.
2. Disable NVS for an offline smoke build. Simpler, but identity secrets, time,
   Wi-Fi and preferences would exercise a different runtime; defer this option.
3. Put NVS in unused app-slot space. Rejected by the current IDF running-region
   write guard; changing the partition table would exceed the selected workflow.

## Plus NVS policy

Add a Plus-only application linker wrapper for `nvs_flash_init`. Call the real
function exactly once. Pass ESP_OK and unrelated errors through unchanged. Map
the two erase-triggering errors to ESP_ERR_INVALID_STATE, log the original
error, and leave migration/repair explicit. This prevents that Arduino fallback,
not all NVS writes: normal NVS initialization and clients can still modify NVS.
No allocation and no SDK/core-source patch is needed. Bootloader and legacy
targets must not gain this wrapper.

## Recovery runner and durable backup

Create a separate reader runner rather than relaxing historical diagnostic
plans. Its prepare/dry-run mode cannot flash. Before any approved app write it
must match USB identity, chip/MAC/security, 16MiB Flash, table and original app;
read and save current 0x9000..0x12000 bytes with mode0600 in a mode0700 per-run
directory; validate device digest; persist a manifest before marking a write
attempt. Existing full-backup SHA and full-slot restore checks remain mandatory.

After capture, reconnect with the tested monitor transport, restore the complete
original app slot and any changed mutable partitions, verify their bytes and
immutable digests, then reset. A restoration error keeps the backup and a
machine-readable report and returns failure. Never restore stale mutable bytes
from the initial full backup. Restore-only mode must work from a saved manifest
without relying on a current USB path or on the reader starting successfully.

## Existing microSD

No full-reader write until the selected card is accessible and its backup is
verified. Prefer a read-only Mac mount through an existing USB-disk mode or a
reader; identify the volume/device before copying. Back up the current tree and
record sizes/hashes and missing paths, including hidden files, so newly created
files can be distinguished. If a complete tree is too large, agree a bounded
test and exact writable paths first; do not equate a few known filenames with
complete protection. Keep backups private and local. SD restoration is separate
from Flash restoration and must be verified before final acceptance.

An explicitly named macOS system index may be excluded and recorded when macOS
denies access; do not generalize this to unreadable reader/user directories.
Rollback preserves macOS indexing/journal directories. It must verify the same
card identity, intact backup and reviewed current content/tree digest before
native rsync, then verify reader-file hashes, empty directories and absence of
trial-created files. New content changes since review invalidate that review.

## Acceptance

Host tests compile the production wrapper and cover both destructive errors,
normal/unrelated returns, non-Plus exclusion and app-only link scope. Runner
tests inject capture/write/restore failures and reject incomplete or wrong-device
backup plans. Build and verify the new S31 image and run required checks, keeping
known baseline CI failures explicit. A later physical run separately records
SD mount, first reader screen, touch/navigation, EPUB turns, memory and restored
ThingsOS. Sleep/current and BOOT press remain separate pending measurements.
