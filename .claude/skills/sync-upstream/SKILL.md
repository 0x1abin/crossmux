---
name: sync-upstream
description: Automate and review CrossMux/CrossPoint upstream repository synchronization. Use when the user asks to sync, update, compare, or merge upstream changes, especially upstream/develop, into this repo through a new branch and pull request instead of committing directly on main or the active target branch. Covers overlap review, upstream-preferred conflict policy, remote/base detection, agent sync branch creation, squash-sync fallback, validation builds, pushing to origin, and opening a draft PR.
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

`run --draft` does the whole happy path:

1. Reads the super.engineering target branch with `sc worktree status --json`
   when available.
2. Fetches `origin/<base>` and `upstream/develop`.
3. Creates a new `agent/sync-upstream-develop-<sha>` branch from `origin/<base>`.
4. Merges upstream without committing yet.
5. Runs repository sanity checks and both build commands.
6. Commits, pushes to `origin`, and opens a draft PR.

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
pio run
pio run -e gh_release_cn
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
- [ ] Both build commands passed, or the PR explicitly explains why they were
      skipped.
- [ ] The PR is a draft unless the user requested a ready-for-review PR.
