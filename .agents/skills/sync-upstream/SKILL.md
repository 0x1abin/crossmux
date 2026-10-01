---
name: sync-upstream
description: Inspect, rehearse, and publish approval-gated upstream synchronization for CrossMux, its FreeInk SDK fork, and its CrossPoint Simulator fork. Use when comparing or syncing upstream changes through isolated candidates and separate draft pull requests.
---

# Sync Upstream

Synchronize three components in order:

1. `Free-Ink/freeink-sdk:main` into `0x1abin/freeink-sdk:main`.
2. `crosspoint-reader/crosspoint-simulator:main` into
   `0x1abin/crosspoint-simulator:main`.
3. `crosspoint-reader/crosspoint-reader:develop` into CrossMux, pinning both
   reviewed fork revisions.

Never commit directly on a component's base branch. Never merge a dependency
pull request or flash hardware as part of this skill.

## Staged workflow

Run one phase at a time from the CrossMux repository root:

```bash
python3 .agents/skills/sync-upstream/scripts/sync_upstream.py inspect
python3 .agents/skills/sync-upstream/scripts/sync_upstream.py start --component sdk
python3 .agents/skills/sync-upstream/scripts/sync_upstream.py publish \
  --component sdk --candidate /path/printed/by/start --draft
```

Repeat `start` and `publish` for `simulator`, then `crossmux`. `start` prepares
an isolated candidate and prints its path; it never commits, pushes, or opens a
pull request. `publish` is a separate, externally mutating step and requires
the user's explicit authorization in the current conversation. There is no
one-command happy path.

To finish one immutable snapshot while parent branches continue moving, pass
the same repeatable pins to every phase:

```bash
--upstream-pin sdk=<sha> \
--upstream-pin simulator=<sha> \
--upstream-pin crossmux=<sha>
```

Each pin must be a full commit SHA reachable from that component's configured
upstream branch. A pinned workflow tolerates a parent branch fast-forward, but
still stops on a rewritten parent history or any Fork/base movement.

`inspect` always checks all three components. If a dependency fork is behind,
sync it even when the incoming CrossMux commit does not change that dependency.
Do not start `simulator` until the SDK fork contains its parent `main`; do not
start `crossmux` until both dependency forks contain their parent `main` and no
corresponding sync pull request remains open.

## Local integration rehearsal

When the user authorizes integration before dependency PRs land, use
`start --local-rehearsal` for SDK, Simulator, then CrossMux, with identical
upstream pins and candidate root. Review and stage each dependency, then repeat
its `start --local-rehearsal` with one approved `--review-note` per review item.
The next stage accepts only that conflict-free, unchanged reviewed index. The
script exports it to a real source directory and records the base commit,
upstream commit and Git source-tree fingerprint in the dependent review state.
Verify reused exports against the reviewed index before consuming them; changed
review scope invalidates the recorded approval. Use those exported sources for
validation builds, including the final firmware.
CrossMux rehearsal starts at the current checkout's HEAD; carry its existing
uncommitted changes into the candidate and review their overlaps too.

Rehearsal never means approval to publish. `publish` rejects these candidates;
formal publication requires fresh candidates with real dependency commits,
renewed validation, and explicit authorization. Keep normal remote gates intact.
Hardware flashing requires separate user authorization.

## Upstream agent-document review

After each `start`, before resolving anything, read the
[agent-guide merge policy](../../../docs/engineering/upstream-merge-policy.md)
and inspect the upstream document delta, even when Git reports no conflicts.
Use the candidate's recorded `base_sha` and `upstream_sha`; follow the policy's
content routing and verification requirements for CrossMux. Review SDK and
simulator guide changes too, using each fork's existing document structure.
The script does not discover cross-path document equivalents or convert content.

## Mandatory manual review

Review every Git conflict, every file changed on both sides since the merge
base (including clean merges), and cross-file or cross-symbol behavioral
overlaps. These include upstream guides whose content has moved into local
documents and legacy entrypoints reintroduced by a clean merge. Use CodeGraph
when available, otherwise `rg` and source inspection, to trace shared behavior.

Before resolving an item missing from `review_items`, repeat `start` with one
`--behavior-overlap` per discovered overlap (a source path or descriptive
source-to-destination mapping). Reuse the component, remote/base options,
candidate root, and upstream pins; `--candidate-root` is the candidate's parent
directory. Check that the printed path, `base_sha`, and `upstream_sha` still
identify the reviewed candidate. If an unpinned upstream has advanced, `start`
may select a new candidate: review that snapshot afresh rather than transferring
old decisions. Do not edit the review state by hand.

Reuse explicit decisions already given in the current conversation for the
same items and revisions. Otherwise ask interactively before resolving them;
general sync authorization is not a resolution decision. Prefer structured
user input, with at most three behavior decisions per batch; otherwise present
numbered options and wait. Each decision must show local and upstream behavior,
compatibility/product impact, and a recommendation. Offer applicable local,
upstream, or combined outcomes without treating the recommendation as approval.
For documents, identify the incoming increment, local destination, and proposed
adaptation or skip reason.

Group files only when they form one behavior chain, listing every covered review
item. Restate each approved choice before applying it. Do not resolve, stage,
commit, push, or open a PR for unanswered items. Preserve CrossMux branding,
apps, releases, translations, and device behavior unless explicitly approved
otherwise; follow the shared rules in [AGENTS.md](../../../AGENTS.md).

When publishing, pass one `--review-note` per approved item in the printed
`review_items` order. The script checks the count and pairs these notes into the
Draft PR body; it cannot verify approval. Document notes identify the upstream
source and local destination or skip reason, accounting for every incoming
hunk. With no review items, the PR records that fact. Keep this interaction in
the agent workflow, without terminal prompts or an extra CLI phase.

## Theme appearance contracts

In the Chinese UI, retain the canonical English theme names from `english.yaml`,
including Classic, Lyra Extended and Lyra Carousel. Translate surrounding settings
labels normally; do not change theme IDs or other languages for this rule.

Upstream synchronization must preserve INX's existing appearance across Reader,
SDK, simulator, and shared rendering changes, including clean Git merges. Keep
its layout, fonts, alignment, spacing, icons, grayscale, selection states,
headers, button hints, and dialogs. New features use the existing INX style;
do not automatically apply upstream theme redesigns to INX.

Explicitly approved exception: INX uses the same keyboard key tables, separated
key geometry, spacing, rounded corners, text centering and interaction states as
other themes. Spanish accent hints and long-press output are unified too; the
former INX Spanish-table exception is revoked. Keep surrounding INX page chrome.
Run `python3 test/inx_navigation/test_unified_keyboard.py` for the active keyboard;
legacy keyboard baseline scenes only validate SDK backward compatibility.

Explicitly approved font exception: all themes, including INX, share the reviewed
upstream Ubuntu Regular/Bold assets at 10pt and 12pt in the standard builtinFonts
directory. Keep INX's font-size choices and layout; glyph weight, width, kerning
and resulting wrapping may change with these shared faces. Do not retain a second
Medium/Bold asset set or duplicate slider registrations for these sizes. INX's
large-text fallback and Chinese fallback behavior remain unchanged. Preserve the
historical visual baseline and record actual page differences for this exception.

For every other theme, use the reviewed upstream snapshot as the visual source
of truth: keep its implementation style, layout, font bindings and font sizes.
CrossMux-only screens use that theme's existing components and styling. Honor
explicitly approved exceptions for fork-only home screens; their shared pages
must follow the selected upstream counterpart. Scope
INX compatibility overrides to INX; shared SDK and simulator defaults must keep
upstream behavior for other themes. Review clean merges for leaked overrides.

Menu structure is part of the contract. For non-INX themes, keep upstream
items in their original categories, relative order and visibility conditions.
Retain CrossMux extension settings in every theme, using the same shared data
source, categories, relative insertion points and action/value bindings. Position
means menu structure, not a fixed pixel coordinate. Do not hide extension settings
or move them into APP to obtain parity. Preserve persistence, Web API, CrossMux
update services and device capability checks.

Home adds one APP entry to the upstream entries. Lyra Carousel explicitly keeps
its carousel home; all other pages follow Lyra. The recent, library and apps
layout settings are INX-only. These exceptions and the unified keyboard above
are approved; all remaining INX appearance protections continue to apply.
Review settings and subpages, reader/library menus, popups and selectors, including
shared fonts, row heights, alignment, scrollbars and draw/hit geometry. Additions
use the selected theme's components without a new theme framework or public API.
Run `python3 test/inx_navigation/test_theme_menus.py` for shared menu assembly,
action bindings and tab draw/hit comparisons against Reader `93e98bb` and the
reviewed SDK. Check theme switching preserves extension values and entries.

Read the [INX compatibility contract](../../../docs/engineering/inx-theme-compatibility.md)
before reviewing UI changes. Trace shared theme tokens, inherited theme defaults,
font rendering, and draw/hit geometry as well as INX-specific files. Prefer the
existing shared theme adaptation points over per-activity workarounds, and retain
upstream clipping, pagination, and input fixes without changing INX styling.
Record relevant behavioral overlaps through `--behavior-overlap` and reuse the
user's explicit theme-preservation decisions when reviewing those items.

Run `python3 test/inx_navigation/test_inx_style_compat.py` before and after the
sync, using the reviewed SDK in the CrossMux validation checkout. Extend existing
coverage for affected headers and dialogs; never replace the visual baseline to
hide a regression. Compare representative INX screens under the same content,
language, settings, orientation, and scale. Draw/hit traces use fixed font metrics
and do not establish whole-screen or physical-device acceptance; report those
checks separately.

Compare non-INX themes directly against the same reviewed Reader and SDK
snapshots, under identical device, content, language, settings, orientation and
scale. Cover home, lists, settings, keyboard, dialogs and touch controls, including
draw and hit geometry. Use `python3 test/inx_navigation/test_upstream_theme_compat.py
--sdk /reviewed/sdk --upstream-sdk /upstream/sdk --upstream-reader-ref <sha>` for
shared component and theme-bridge parity; this does not establish whole-screen
acceptance. Do not treat successful builds or INX checks as non-INX parity.
An unresolved INX regression or non-INX upstream appearance mismatch blocks
publication.

Cover Grid uses persisted ID 6; retain Carousel 4 and INX 5. Its home follows
upstream with APP appended, and shared pages follow Lyra. Gate it on PSRAM;
on unsupported devices hide the choice and render/display Lyra without
rewriting the saved selection. Validate OPDS on/off, empty libraries, missing
covers, low memory and resource release. Candidate comparison must use its
actual production metrics, fonts and rendering paths, never substitute upstream
metrics on both sides. Use simulator input scripts and screenshots for whole-page
comparison; unexplained differences block an upstream-parity delivery.

Explicitly approved checkbox increment for the fixed rehearsal: Reader
`93e98bb` plus `d1509d0`, SDK `5deb923c` plus `e41f683e`, Simulator `8699595`,
on CrossMux base `9d02f498`. Record increments separately from the pinned base;
do not advance other upstream functionality or rebase main during this delivery.
Non-INX boolean settings, reader menu and toolbar rows use the upstream checkbox
and hit geometry. The user additionally approved uniform setting controls in INX:
boolean settings use the same checkboxes, and slider adjustment dialogs use the
common Lyra control geometry and fonts. Preserve INX settings categories, row
layout, page chrome and ordinary option pickers; reader menu/toolbar textual
states remain unchanged. Multi-valued settings keep their existing selection type.
Compare with the fixed reference plus those exact increments; historical INX
baselines remain immutable. A later latest-main/upstream sync is a separate task.

## Validation

After document verification, `publish` still performs component-specific checks
unless `--skip-builds` was explicitly authorized:

- SDK: its four existing host test scripts, then the CrossMux PlatformIO
  validation and any repeatable `--extra-build-env` values.
- Simulator: its host compatibility self-test, all four CrossMux simulator
  environments, and the CrossMux CMake/CTest host suite.
- CrossMux: index/conflict-marker checks, `git diff --check`, `pio run`,
  `pio run -e gh_release`, and extra build environments.

SDK integration builds export the reviewed Git index into a real directory.
This includes staged, uncommitted resolutions while excluding Git metadata and
untracked/ignored debug artifacts; directory symlinks break PlatformIO's
framework dependency path matching. Conflict-marker checks scan text files,
so binary font bytes cannot produce false conflicts.

Report local checks, dependency Draft PRs, CrossMux Draft PR, CI, deployment,
and physical-device acceptance separately.
