# Upstream-Merge Policy for the Agent Guide

Use this policy when an upstream sync changes `AGENTS.md`, `.agents/skills/`,
or legacy upstream paths such as `CLAUDE.md`, `.skills/SKILL.md`, and
`.claude/skills/`. It covers content changes and path/type conflicts, including
a clean merge that reintroduces a removed entrypoint or directory.

## Local structure

- `AGENTS.md` is a **regular file** and the canonical thin map: CrossMux identity,
  critical rules, common commands, and links to the engineering reference.
- `AGENTS.md` is the sole agent entrypoint; there are no platform-specific aliases.
- `.agents/skills/` is a **real directory** containing the on-demand skills,
  scripts, tests, templates, and references.
- Detailed firmware guidance lives in [`docs/engineering/`](index.md).

An upstream tree may still store its guide in `.skills/SKILL.md` or use another
entrypoint layout. Those paths are compatibility inputs, not local destinations.
Do not restore legacy agent paths or replace CrossMux policy with upstream policy.

## Resolution procedure

1. Follow the task's authorized sync/review procedure. Preserve the CrossMux
   guide structure and inspect every affected guide path, even if Git reports
   no conflict. A changed symlink target can be as significant as changed text.
2. Compute the incoming delta against the merge base, not the entire upstream
   monolith against the local map. During a merge, `MERGE_HEAD` names the
   incoming revision:

   ```bash
   git diff "$(git merge-base HEAD MERGE_HEAD)..MERGE_HEAD" -- \
     AGENTS.md .agents/skills CLAUDE.md .skills/SKILL.md .claude/skills
   ```

   Outside an active merge, use the candidate's recorded upstream revision in
   place of `MERGE_HEAD`. Inspect the referenced upstream body when the delta
   only changes an entrypoint or moves the guide.
3. Route each relevant change using the table below. Preserve the CrossMux
   mission, `main` PR target, unified languages, apps, and target-specific
   hardware budgets. Do not import an upstream feature freeze automatically.
4. Leave the resolved tree with a regular `AGENTS.md` and a real
   `.agents/skills/` directory, without legacy entrypoints or aliases. Move
   applicable upstream skill changes into the corresponding local skill.
   Resolve modify/delete or symlink conflicts explicitly; do not apply
   `checkout --ours` to a deleted legacy path and assume the migration survives.
5. Validate the result, account for every upstream hunk in the review, and stage
   only the reviewed resolutions when the sync task permits it. Commit and
   publish only within the user's authorization.

| Incoming change | Destination |
|---|---|
| Deep technical content | Matching engineering topic below |
| New critical invariant | A short AGENTS rule plus details in the topic doc |
| New topic without a home | A focused engineering doc and links from AGENTS and the index |
| Identity, quick reference, or scope wording | AGENTS, reconciled with CrossMux policy |
| Rewording of relocated content | The topic doc only |
| Upstream-only policy or inapplicable behavior | Explain why it was skipped in the sync review |

## Topic routing

Keep this table aligned with [`index.md`](index.md).

| Guide topic | Destination doc |
|---|---|
| Hardware specs, the Resource Protocol, platform detection | [hardware-constraints.md](hardware-constraints.md) |
| Memory safety / RAII, `new` / `malloc` / `makeUniqueNoThrow`, OOM handling | [memory-and-allocation.md](memory-and-allocation.md) |
| `string_view`, IRAM/flash cache, ISR↔task, RISC-V alignment, template/`std::function` bloat, ArduinoJson v7 | [esp32-pitfalls.md](esp32-pitfalls.md) |
| PlatformIO, build environments, critical build flags, `platformio.local.ini` | [build-system.md](build-system.md) |
| Directory structure, HAL, singletons, activity lifecycle, FreeRTOS tasks, fonts | [architecture-and-patterns.md](architecture-and-patterns.md) |
| Naming, header guards, error-handling philosophy | [coding-standards.md](coding-standards.md) |
| Orientation-aware logic, logical button mapping, UITheme, `tr()` | [ui-and-input.md](ui-and-input.md) |
| Generated files & build-artifact workflow (HTML, i18n, fonts) | [generated-files.md](generated-files.md) |
| Build/monitor commands, crash playbook, verification checklist, CI | [testing-and-debugging.md](testing-and-debugging.md) |
| Repo detection, git rules, branch naming, commit format, when to commit | [git-workflow.md](git-workflow.md) |
| Cache structure, invalidation, format versioning | [cache-management.md](cache-management.md) |
| Unified content profiles, embedded CJK fonts | [chinese-build.md](chinese-build.md) |

## Verification

- `AGENTS.md` remains a regular file and at most about 150 lines; move depth
  into engineering docs instead of expanding the map.
- `.agents/skills/` is a real directory, and all skill resources resolve from it.
- No legacy agent entrypoints or directories survive. Old path references are
  limited to upstream compatibility documentation; third-party copyright and
  source attribution remain intact.
- Relative links and touched Markdown anchors resolve.
- Every incoming hunk is applied, routed, or explicitly skipped with a reason.
- Guide-only resolutions need no firmware build; run the relevant checks for
  any accompanying code changes. Do not trigger a sync workflow to test docs.
