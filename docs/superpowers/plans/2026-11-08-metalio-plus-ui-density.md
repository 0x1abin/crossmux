# Metalio Plus interface density plan

Execute inline in the current isolated checkout; continued device development
is authorized. No new dependencies or automatic commits/restoration.

Goal: make interface text and bottom navigation readable at native resolution.
Spec: ../specs/2026-11-08-metalio-plus-ui-density.md

- [x] Trace native framebuffer geometry, font resources and navigation metrics.
- [x] Enable the existing high-density preset in the Plus build flags only.
- [x] Run existing relevant font/chrome/layout checks and focused review.
- [x] Build S31, archive unique artifacts, verify startup/memory/image/backups.
- [x] Update-and-keep the original recovery association and capture runtime.
- [x] User accepts larger UI/navigation, alignment and normal reading/refresh.
- [x] Record actual memory, test limitations and physical feedback.

Review focus: any ReadPico-only assumptions behind the generic density flag;
font registration/Chinese fallback correctness; footer/content fit; bitmap/tap
geometry agreement; no activation of grayscale or loss of differential cadence.

Preflash result: native build passed; static RAM108032 (+64), linked Flash6509170.
Full host suite1532/1538 with identical six baseline compiler-error failures.
Font resources11 cases (one optional skip), high-density layout3 pass; extra
chrome5 pass/one optional skip/one unchanged ReadPico fixture warning-as-error.
Review found no actionable issues. Native ELF includes NotoSans14/16 and the
native56px recent-tab asset. No new dependencies or SDK changes.

Physical result: user reports 大小合适，显示与点按正常 after the requested
menu/Home/reopen/two-page check. All captured refreshes committed; no captured
panic/error markers. Candidate retained, collector stopped intentionally;
original recovery association unchanged. Evidence: parent
logs/plus-ui-density-acceptance.json. Other themes/orientations and endurance
remain outside this bounded acceptance.
