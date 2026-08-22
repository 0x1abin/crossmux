---
name: sync-upstream
description: Automate and review CrossMux/CrossPoint plus FreeInk SDK upstream synchronization. Use when the user asks to sync, update, compare, or merge upstream changes. Covers the SDK-first two-PR gate, CrossMux merge, validation, and draft PR creation.
---

# Sync Upstream

Use this skill when syncing upstream changes into the current repo. The default
source is `upstream/develop`, because that is the current upstream integration
branch for this project. Never commit directly on `main` or the
super.engineering target branch.

## Primary Command

From the repository root, run:

```bash
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py inspect
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py run --draft
```

Private forks can append repeatable build environments, for example
`--extra-build-env gh_weread_pro`.

`run --draft` uses a two-stage gate:

1. Compares `Free-Ink/freeink-sdk/main` with `0x1abin/freeink-sdk/main`.
2. If the fork is behind, creates `agent/sync-freeink-sdk-main-<sha>`, runs both
   SDK host suites and CrossMux `default`, `sticky`, `eego_a4`, and `murphy_m4` builds, opens
   an SDK draft PR, then stops without changing CrossMux.
3. After that PR is merged, reads the super.engineering target branch with `sc worktree status --json`
   when available.
4. Fetches `origin/<base>` and `upstream/develop`, verifies the upstream SDK
   gitlink exists in the fork, and stages the CrossMux merge.
5. Preserves the fork URL in `.gitmodules`, updates the gitlink to fork `main`,
   validates, and opens the CrossMux draft PR.

Check the `inspect` output before `run`. If `base_branch` is not the intended
CrossMux integration branch, pass `--base-branch main` or set the worktree target
outside this skill before continuing.

If the merge stops with conflicts, resolve them, stage only the intended files,
then continue with:

```bash
python3 .claude/skills/sync-upstream/scripts/sync_upstream.py publish --draft
```

## Conflict Policy

- Review behavioral overlap before resolving file conflicts. When upstream now
  provides the same capability and satisfies CrossMux business requirements,
  use the upstream implementation and remove the duplicate local path.
- Keep a CrossMux-local implementation only for a requirement upstream does not
  meet. Limit it to that gap and record the reason in the PR body; do not retain
  parallel implementations merely because the local one landed first.
- For Chinese support, prefer upstream's generic CJK parsing, layout, rendering,
  and font mechanisms. Preserve CN-build-only behavior only where upstream does
  not provide the same offline, first-boot, glyph-coverage, distribution, or
  flash-budget guarantees.
- `.skills/SKILL.md` is a thin map. When upstream changes it, follow
  `docs/engineering/upstream-merge-policy.md`: keep the map thin, route deep
  content into `docs/engineering/`, and do not drop upstream hunks silently.
- Preserve CrossMux-local behavior, branding, docs, apps, and release settings
  unless the user explicitly asks to remove them.
- For i18n YAML, preserve a union of flat keys. Keep Chinese build behavior and
  existing translations unless an upstream key intentionally replaces them.
- For submodules, verify the old directory is clean before removing stale
  directories, then run `git submodule update --init --recursive`.
- If an upstream change is intentionally skipped as out of scope, mention the
  skipped hunk in the PR body.
- When an upstream change alters a cache layout or pagination, advance the
  CrossMux per-flavor cache versions above every shipped value and update
  `docs/file-formats.md`.

## Validation

The script runs these before publishing unless `--skip-builds` is explicitly
used:

```bash
git diff --check
git diff --cached --check
pio run -e default -e sticky -e eego_a4 -e murphy_m4
```

It also checks for unresolved index entries and conflict markers in tracked
files.

## Self-Review

- [ ] The current branch is not the base branch.
- [ ] Only the upstream sync is in the diff; unrelated local files are not
      staged.
- [ ] `.skills/SKILL.md` still follows the thin-map policy.
- [ ] Submodules are initialized and no stale SDK directory was staged by
      accident.
- [ ] SDK host tests and all four device builds passed, or the PR explicitly explains why they were
      skipped.
- [ ] The PR is a draft unless the user requested a ready-for-review PR.
