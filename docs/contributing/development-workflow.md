# Development Workflow

This page defines the expected local workflow before opening a pull request.

## 1) Fork and create a focused branch

- Fork the repository to your own GitHub account
- Clone your fork locally and add the upstream repository if needed
- Enable repo hooks once per clone: `git config core.hooksPath .githooks && chmod +x .githooks/pre-commit`

- Branch from `develop`
- Keep each PR focused on one fix or feature area

## 2) Implement with scope in mind

- Confirm your idea is in project scope: [SCOPE.md](../../SCOPE.md)
- Prefer incremental changes over broad refactors

## 3) Run local checks

```sh
./bin/ci-check
```

This runs the same formatting, static analysis, `default`/`sticky` firmware
builds, and host unit tests as CI. It stops at the first failure and does not
modify source files.

Before the first run, initialize submodules with
`git submodule update --init --recursive`. The script does not install its
required tools: PlatformIO, clang-format 21+, CMake, and Ninja. See
[Getting Started](./getting-started.md) for the core setup.

## 4) Open the PR

- Target `develop` (the repository's default branch)
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
