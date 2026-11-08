# Plus Differential Refresh Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline in the existing
> isolated checkout. Explicit refresh debugging is authorized; no commits/restores.

**Goal:** Comfortable ordinary page turns with periodic/explicit full cleanup.
**Architecture:** Keep native checked driver and vendor PSRAM baseline; pass
Full/Half/Fast into the BSP and select existing GC16/GL16/DU appropriately. Plus
periodic reader cleanup requests Full. No waveform, UI setting or heap changes.
**Tech Stack:** Existing Arduino4/IDF6.1 S31, real C++ driver and reader tests.
**Spec:** ../specs/2026-11-08-metalio-plus-differential-refresh.md

## Global constraints

- V1.4 S31 E0470A14-AF native1216x684; logical684x1216, binary only.
- Preserve frozen 74 vendor blobs, SDK gitlink, VCOM/temperature/rail timing.
- No new heap/frame/task/async/native grayscale, dependency or cadence default.
- Original recovery association remains; restore only on request; USB deferred.

## Review focus

- Previous framebuffer must remain the last committed physical image.
- A failed draw/clear/power request forces clean retry; off failure stays latched.
- Baseline invalidated on resync/sleep, retained on ordinary checked power-off.
- Actual ReaderRefresh periodic clean is Full; other targets/deferred debt unchanged.
- Window and split paths honor modes and cannot claim unsupported async or gray.

### Task 1: Production driver and cadence regression

- [x] RED: repeated ordinary frames must not call clear or from-white again;
      Half uses GL16, Fast DU, explicit Full/invalidated uses original clean path.
- [x] RED: actual Plus reader cadence yields Full/Fast/Fast at interval three.
- [x] Implement minimal mode route and baseline lifecycle, no new waveform.
- [x] GREEN: failure/recovery/mapping/sleep/window/split and legacy cadence cases.

### Task 2: Native build and physical refresh acceptance

- [x] Affected SDK and repository checks; focused review with findings closed.
- [x] Build, archive unique artifact, verify native config/image/startup/frozen files.
- [x] Update-and-keep original recovery manifest; never restore automatically.
- [x] User checks repeated text pages/periodic clean/manual full/Home/popups/sleep.
- [x] Record flash/ghosting/contrast feedback and measured durations separately.

Result: retained candidate 7fccbb165e4a7a383383640f7702e09b3de5bf06e6348cf6604de3334d584ff3.
Text flash/clarity, manual/menu/Home and one sleep/wake sequence accepted.
Observed continuous cleanup interval 15 pages; sampled text driver median
453ms DU / 1611ms Full. Image-heavy optical quality and long stress remain
outside this bounded acceptance. Parent logs contain exact artifacts/evidence.
