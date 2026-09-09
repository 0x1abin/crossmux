# Upstream-Merge Policy for the Agent Guide

Use this policy to adapt upstream agent guidance to CrossMux. It covers
`AGENTS.md`, `.agents/skills/`, legacy inputs such as `CLAUDE.md`,
`.skills/SKILL.md` and `.claude/skills/`, and any relocated guide bodies.
The [Sync Upstream skill](../../.agents/skills/sync-upstream/SKILL.md#mandatory-manual-review)
owns candidate management, interactive decisions, and publication records.
This document owns content routing and the resulting CrossMux layout.

## Local structure

- `AGENTS.md` is the sole agent entrypoint, a regular file containing CrossMux
  identity, critical rules, common commands, and reference links.
- `.agents/skills/` is a real directory for task-specific workflows and their
  scripts, tests, templates, and references.
- Deep engineering guidance belongs in existing topics listed in the
  [engineering index](index.md).

Legacy upstream paths are migration inputs, not destinations. Do not restore
platform-specific entrypoints or Claude automation. Preserve CrossMux's mission,
`main` PR target, unified languages, lightweight apps, and target-specific
hardware budgets when adapting upstream wording.

## Inspect the incoming delta

Compare the upstream revision with the merge base, not its whole guide with
our thin map. In a sync candidate, use the recorded `base_sha` and `upstream_sha`
as `sync_base_sha` and `sync_upstream_sha`. For a manual merge without candidate
state, use `HEAD` and `MERGE_HEAD` respectively, before creating the merge commit.
Run in the repository being merged:

```bash
sync_merge_base=$(git merge-base "$sync_base_sha" "$sync_upstream_sha")
git diff --name-status --find-renames "$sync_merge_base" "$sync_upstream_sha"
git diff "$sync_merge_base" "$sync_upstream_sha" -- \
  AGENTS.md .agents/skills CLAUDE.md .skills/SKILL.md .claude/skills
```

Review clean merges as well as conflicts. Use the unfiltered path list to find
additions, deletions, renames, and type changes outside the known paths. Read
referenced bodies and symlink targets from the recorded upstream tree; the
merged working tree may have lost them. An entrypoint-only change still needs
its referenced content checked.

## Route the content

Account for every incoming hunk through the sync skill's review procedure (or
the authorized manual merge review). Adapt commands, paths, and authorization
rules to CrossMux rather than copying upstream workflow instructions verbatim.

| Incoming change | Local destination or disposition |
|---|---|
| Deep technical guidance or rewording of relocated content | Existing topic selected from the [engineering index](index.md) |
| Operational workflow and supporting resources | Corresponding `.agents/skills/` skill; preserve resource permissions and repair links |
| New critical invariant | Short AGENTS rule plus details in the relevant topic |
| New topic without an existing home | Focused engineering document with index and AGENTS links |
| Identity, quick reference, or scope wording | AGENTS, reconciled with CrossMux policy |
| Upstream-only policy, feature freeze, or inapplicable behavior | Skip with a reason in the review |

Resolve modify/delete and symlink conflicts explicitly. Keeping the local
version of a legacy path does not prove its useful content reached the current
destination. Avoid duplicating a rule in AGENTS, a skill, and an engineering
topic; keep detail at its destination and link to it.

## Verification

- AGENTS remains a regular, thin map (about 150 lines or fewer), and skills live
  in a real directory with working resources.
- No legacy entrypoints or directories survive; legacy path references serve
  upstream compatibility only. Preserve third-party copyright and attribution.
- Relative links, touched Markdown anchors, and `git diff --check` pass.
- Every incoming hunk has a reviewed destination or an explicit skip reason.

Standalone guide edits need no firmware build. For a sync, the skill's
publication checks and authorization requirements still apply; document checks
do not authorize skipping builds or triggering remote workflows.
