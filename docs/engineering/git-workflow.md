# Git Workflow & Repository Awareness

> Deep reference for [AGENTS.md](../../AGENTS.md). Verify repository context
> before git operations. The contributor-facing flow is in
> [development-workflow.md](../contributing/development-workflow.md).

## Verify the checkout

At the start of work, check the repository root, branch, remotes, and local
changes. A worktree may have a detached HEAD, and a contributor's fork may use
remote names differently.

```bash
git rev-parse --show-toplevel
git branch --show-current
git remote -v
git status --short
```

Preserve unrelated user changes. Confirm the intended remote and branch before
any push, merge, reset, or other history-changing operation.

## CrossMux contribution target

- Unqualified PR requests target **`0x1abin/crossmux:main`**. In the maintainer
  checkout, `origin` is `0x1abin/crossmux`; push the feature branch there when
  authorized by the PR request. Do not ask for the default target again.
- An explicit user-supplied repository or base branch overrides that default.
  If the configured remote differs, resolve the destination before pushing.
- Contributors using a personal fork push there and open their PR against
  CrossMux `main`, not upstream CrossPoint `develop` or `master`.
- Create a focused branch from CrossMux `main`; agents use `codex/<topic>` by
  default. Human contributors may use `feature/`, `fix/`, `refactor/`, or `docs/`.

## Upstream synchronization is a separate task

Do not fetch and merge upstream automatically when starting an ordinary change.
CrossMux integrates its SDK, simulator, and reader upstreams through isolated
candidates; the current sources and procedure are in the
[sync-upstream skill](../../.agents/skills/sync-upstream/SKILL.md).

Only synchronize when the task calls for it. Preserve fork-specific behavior
and follow the [guide merge policy](upstream-merge-policy.md) when upstream
changes agent documentation. Publishing or merging is not implied by a read-only
comparison or local rehearsal.

## Commits and verification

Commit only when the user explicitly requests it. A completed feature, passing
checks, or successful hardware test is not independent permission to commit.
A request to create a PR includes preparing and publishing that PR's commits.
Do not commit directly on the base branch.

Before staging, inspect the diff and working-tree status. Never force-add
ignored artifacts such as `.pio/`, `compile_commands.json`, or
`platformio.local.ini`. Generated sources follow
[generated-files.md](generated-files.md); edit their inputs and use the
prescribed generator rather than hand-editing generated output.

Use semantic commit and PR titles such as `docs: align guides with CrossMux` or
`fix: handle malformed epub`. Keep each change focused. Report checks actually
run and hardware validation still needed; documentation-only changes do not
require device tests. Follow [testing-and-debugging.md](testing-and-debugging.md)
and the contributor workflow for checks appropriate to code changes.
