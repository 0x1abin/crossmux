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
