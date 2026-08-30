# Upstream-Merge Policy for `CLAUDE.md` / `.skills/SKILL.md`

> How to resolve conflicts on the development guide when syncing upstream into
> `main`. This is the rule the manual sync routine and any human resolver must
> follow. Read it whenever `.skills/SKILL.md` appears in a sync PR's conflict list.

## The structural invariant

- `CLAUDE.md` is a **symlink** → `.skills/SKILL.md` (identical on both branches,
  so the symlink itself never conflicts). The real content lives in
  `.skills/SKILL.md`.
- On `main`, `.skills/SKILL.md` is a **thin map** (~90 lines): identity,
  Golden Rules, a quick reference, and a topic→doc table. The deep
  firmware-engineering reference lives in [`docs/engineering/`](index.md).
- **Upstream keeps the old monolithic version** of `.skills/SKILL.md` and keeps
  editing it. This is expected. Because the two diverged structurally, *every*
  sync that touches this file produces a content conflict on `.skills/SKILL.md`.

**The job is never to merge the two files line-by-line.** It is to keep
`main`'s thin map and re-home upstream's new content the same way the original
refactor did.

## What is "live" in the map

Only these parts of `.skills/SKILL.md` may receive content directly:

- Project / Mission header
- **AI Agent Identity and Cognitive Rules**
- **Golden Rules — Non-Negotiable Invariants** (one line each)
- **Quick Reference** (singletons, platform detection, core commands)
- **The Map** table
- Philosophy footer

Anything upstream adds that is *deep technical detail* does **not** belong in the
map — it goes into `docs/engineering/`.

## Resolution procedure

When a sync merge reports a conflict on `.skills/SKILL.md`:

1. **Keep ours (the thin map).** The structure is the whole point.
   ```bash
   git checkout --ours -- .skills/SKILL.md   # CLAUDE.md symlink auto-resolves (identical both sides)
   ```

2. **Compute the upstream delta this sync actually brought** — do not diff the
   whole monolith against the thin map (that's all noise). During
   `git merge upstream/master`, `MERGE_HEAD` is the upstream tip:
   ```bash
   git diff "$(git merge-base HEAD MERGE_HEAD)..MERGE_HEAD" -- .skills/SKILL.md
   ```
   This shows exactly what upstream added/changed since the last shared point.

3. **Classify and route each hunk of that delta:**

   | Upstream change | Where it goes |
   |---|---|
   | Deep technical content for an existing area | Edit the matching `docs/engineering/<topic>.md` (routing table below) |
   | A new non-negotiable invariant | Add a one-line **Golden Rule** to the map **and** the full detail to the topic doc |
   | A brand-new area with no home | Create `docs/engineering/<new>.md`, then add a row to the map's table **and** to [`index.md`](index.md) |
   | A change to a section still living in the map (Identity / Cognitive Rules, Quick Reference, Philosophy) | Apply it directly in `.skills/SKILL.md` |
   | Pure reword / typo fix of already-relocated content | Apply in the topic doc only |
   | Irrelevant to `main` (e.g. BLE — `main` deliberately carries no BLE) | Skip it, and note the omission in the sync PR |

4. **Stage and finish.** `git add .skills/SKILL.md` plus every touched
   `docs/engineering/*` file, then complete the merge.

## Routing table (section → destination)

Canonical key for step 3 — keep this in sync with [`index.md`](index.md).

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

## Hard rules

- **Never** resolve by taking upstream's monolithic `.skills/SKILL.md` wholesale —
  that silently undoes the refactor and re-bloats the map.
- The map stays **≤ ~150 lines**. If a resolution grows it past that, the content
  belonged in `docs/engineering/`, not the map.
- **Nothing is dropped silently.** Every upstream hunk is either routed to a doc,
  promoted to a Golden Rule, applied in the map, or explicitly noted as
  out-of-scope in the PR.

## Verification (after resolving)

1. `wc -l CLAUDE.md` is still ≤ ~150 lines.
2. Every relative link in the map and the touched `docs/engineering/*` files
   resolves (see the link checker pattern in the repo).
3. The upstream delta from step 2 is fully accounted for (routed / promoted /
   applied / explicitly skipped).
4. `pio run` is unaffected — docs are not compiled.
