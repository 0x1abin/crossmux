# CrossMux Agent Skills

Project skills live directly in `.agents/skills/`, alongside their scripts,
tests, templates, and references. Each skill's description identifies the tasks
it helps with.

[AGENTS.md](../../AGENTS.md) is the sole agent entrypoint. It holds shared rules
and links to the engineering reference; these skills add task-specific decision
procedures on demand.

| Skill | Use when… |
|---|---|
| `heap-discipline` | Allocating memory, buffers, strings, or caches |
| `control-flow-clarity` | Writing branching logic, state flags, or dispatch |
| `hal-and-abstractions` | Touching storage, input, display, settings, i18n, or rendering |
| `scope-discipline` | Adding a feature, activity, service, setting, or dependency |
| `refactor-for-review` | Refactoring, cleaning up, or preparing a focused change for review |
| `design-xteink-html-prototypes` | Creating X3/X4 HTML UI prototypes with accurate geometry and input behavior |
| `sync-upstream` | Inspecting or rehearsing upstream synchronization and publishing separately authorized draft PRs |

## Maintaining skills

Edit the `SKILL.md` in the relevant directory. Keep it focused on decisions
specific to that task; link to shared rules instead of copying them. Anchor
references on maintained files, APIs, types, and macros rather than line numbers
that drift. Keep descriptions specific enough to select the skill for the
right work.
