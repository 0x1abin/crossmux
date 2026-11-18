# Metalio Plus cover/image grayscale plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans task-by-task.

**Goal:** Preserve image/cover gray values from source to the Plus native target.
**Spec:** [Image design](../specs/2026-11-13-metalio-plus-image-gray-design.md).

## Global Constraints

- Third subsystem; compile environment repair remains fourth.
- No physical acceptance, flash, SD changes or commits while user is away.
- HAL and existing native loan; no additional framebuffer or vendor edits.
- Existing C3, older native drivers, BW images and cache layouts retain defaults.

## Review Focus

Gray8 fidelity, partial/cancelled render cleanup, one activation, BW cover-cache exclusion, cache invalidation and source decoding bounded memory.

### Task 1: Native PNG and image-page integration
- [x] Add real decoder/renderer tests for16 levels, scaling and inactive-target rejection; observe RED.
- [x] Add PNG native writing, bypass four-level cache for native JPEG/PNG, route eligible Plus image pages through a single native commit.
- [x] Keep GIF four-level compatibility and legacy rendering paths; run focused checks.

### Task 2: Gray cover caches and UI frames
- [x] Test Gray8 converters and default format compatibility before implementation.
- [x] Add explicit Gray8 outputs and independent Plus caches; native bitmap/crop/clear/display routes and bounded stack scope for cover screens.
- [x] Disable BW-only cover shortcuts on native frames; check failure cleanup and regression suites.

### Task 3: Existing image consumers and preserved candidate
- [x] Prevent immediate retries after failed native viewer/sleep hardware commits.
- [x] Preserve fixture/source and document GIF/transparent overlay limitations and pending physical checks.
- [x] Run focused and full host checks, then proceed to compiler repair.
