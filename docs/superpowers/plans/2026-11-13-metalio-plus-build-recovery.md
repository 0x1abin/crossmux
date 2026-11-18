# Metalio Plus compiler recovery and final verification

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task.

**Goal:** Reproduce isolated native builds and preserve an unflashed candidate for return acceptance.
**Spec:** [Build recovery](../specs/2026-11-13-metalio-plus-build-recovery-design.md).

## Global Constraints
- Last requested subsystem. No commits/pushes, device/SD writes or physical acceptance.
- Existing authorized dependencies only, inside isolated dev. Preserve fixed versions and reversible repair evidence.
- One fresh whole-increment reviewer at the end, per executing-plans.

## Review Focus
Framework identity/reinstall cause, cached compiler/IDF provenance, protected flash scope, C3 resource/behavior compatibility, native failure safety and pending physical limitations.

### Task 1: Diagnose and restore local compiler
- [x] Reproduce the failure, inspect verbose tool/package paths and preserve manifests before changing anything.
- [x] Repair the proven local cause within existing authorized tools; no global install/upgrade.
- [x] Build Plus with critical flash/memory/partition flags intact, fix new compiler findings and rerun.

### Task 2: Final evidence and review
- [x] Format owned code; run full relevant code checks and shared-target checks using existing caches.
- [x] Request one fresh complete-increment review (unstaged source plus new files, both repos); resolve material findings and rerun affected checks.
- [x] Preserve final source/binaries/manifests and one consolidated return acceptance document, no device mutation.
