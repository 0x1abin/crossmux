# Development Workflow

This page defines the expected local workflow before opening a pull request.

## 1) Fork and create a focused branch

- Fork `0x1abin/crossmux` to your own GitHub account, or use your authorized
  maintainer checkout.
- Clone that repository and verify remotes before pushing; a personal fork
  and the maintainer checkout can use different remote names.
- Enable repo hooks once per clone: `git config core.hooksPath .githooks && chmod +x .githooks/pre-commit`

- Branch from CrossMux `main`; agents default to `codex/<topic>`
- Keep each PR focused on one fix or feature area

## 2) Implement with scope in mind

- Confirm your idea is in project scope: [SCOPE.md](../../SCOPE.md)
- Prefer incremental changes over broad refactors

## 3) Run local checks

```sh
./bin/ci-check
```

This runs formatting, static analysis, the firmware environments listed in
`bin/ci-check`, and host unit tests. GitHub CI uses its own target matrices.
The local script stops at the first failure and does not modify source files.

Before the first run, initialize submodules with
`git submodule update --init --recursive`. The script does not install its
required tools: PlatformIO, clang-format 21+, CMake, and Ninja. See
[Getting Started](./getting-started.md) for the core setup.

For documentation-only changes, verify local links and anchors, commands
against repository configuration, and `git diff --check`; no firmware build is
needed. Changes to scripts or workflows need a focused runnable check.

Do not merge CrossPoint upstream as part of ordinary feature work. Upstream
synchronization is a separate task using the
[sync-upstream procedure](../../.agents/skills/sync-upstream/SKILL.md).

## 4) Open the PR

- Target `0x1abin/crossmux:main` unless the task explicitly names another target
- Use a semantic title (example: `fix: avoid crash when opening malformed epub`)
- The GitHub PR title check depends on PR metadata and is not run by `./bin/ci-check`
- Fill out `.github/PULL_REQUEST_TEMPLATE.md`
- Describe the problem, approach, and any tradeoffs
- Include reproduction and verification steps for bug fixes

## 5) Review etiquette

- Be explicit and concise in responses
- Keep discussions technical and respectful
- Assume good intent and focus on code-level feedback

For community expectations, see [GOVERNANCE.md](../../GOVERNANCE.md).
