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
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py inspect
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py start --component sdk
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py publish \
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

## Mandatory manual review

Do not choose a side automatically. Manual review is required for:

- every Git-unmerged path;
- every file changed on both sides since their merge base, even if Git merged
  it cleanly;
- different files or symbols that replace, duplicate, or alter the same
  behavior or call path.

Before resolving anything, ask the user interactively. Prefer the structured
user-input control when it is available and present at most three behavior
decisions per batch. Otherwise ask explicit numbered options and stop for the
answer. A static conflict report is context, not completed review.

Present each decision with:

1. the local behavior;
2. the upstream behavior;
3. the compatibility and product impact;
4. a recommendation without treating it as approval.

Offer the local version, upstream version, and any safe combined resolution as
mutually exclusive options, putting the recommendation first without treating
it as approval. Files may share one question only when they form one behavior
chain; list every covered review item in that question. After each answer,
restate the locked choice before applying it, then continue asking until every
review item is covered. Do not resolve, stage, commit, push, or open a pull
request for an unanswered item.

Pass one `--review-note` per approved review item when publishing; the script
refuses to publish a candidate with review items and no recorded decision.
Include every decision in the Draft PR body. If there were no review items,
record that fact instead. Keep interaction in the agent workflow: do not add a
terminal prompt or another CLI phase between `start` and `publish`.

Use CodeGraph when available to trace shared symbols and call paths; otherwise
use `rg` and source inspection. Preserve CrossMux-specific branding, apps,
release settings, translations, and device behavior unless the user explicitly
approves changing them. Continue to follow the cache-version, thin-map,
submodule, i18n, HAL, input, and allocation rules in `AGENTS.md` and
`docs/engineering/`.

## Validation

`publish` performs component-specific checks unless `--skip-builds` was
explicitly authorized:

- SDK: its four existing host test scripts, then the CrossMux PlatformIO
  validation and any repeatable `--extra-build-env` values.
- Simulator: its host compatibility self-test, all four CrossMux simulator
  environments, and the CrossMux CMake/CTest host suite.
- CrossMux: index/conflict-marker checks, `git diff --check`, `pio run`,
  `pio run -e gh_release`, and extra build environments.

Report local checks, dependency Draft PRs, CrossMux Draft PR, CI, deployment,
and physical-device acceptance separately.
