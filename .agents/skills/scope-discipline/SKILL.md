---
name: scope-discipline
description: Evaluate CrossMux feature scope and resource tradeoffs when adding an activity, app, service, setting, library, or dependency. Reading is the core; lightweight e-ink extensions are allowed subject to RAM, Flash, power, and maintenance costs.
---

# Scope Discipline

[SCOPE.md](../../../SCOPE.md) is the source of truth. CrossMux includes
lightweight games and tools, reading analytics, standby customization, and
on-demand services alongside the reader. Do not apply an upstream blanket ban
on apps, themes, or network connectors to this fork.

## Before adding surface

1. Name the concrete reading or lightweight e-ink use case. Check it against
   the scope document; distinguish a small app or on-demand service from a
   general-purpose suite or an unbounded background workload.
2. Check whether an existing activity, setting, helper, or SD asset already
   covers the need. Extend that mechanism before adding a parallel one.
3. Account for steady-state and peak RAM, the largest required heap block,
   Flash/OTA headroom, and persistent storage. ESP32-C3's approximately 380KB
   usable RAM and lack of PSRAM constrain shared code; S3 capabilities belong
   to specific targets. Mark estimates and missing measurements explicitly.
4. Describe network/task lifetime, idle sleep, and cleanup on exit. A foreground
   live mode needs defined retry and sleep behavior; it is not permission to
   keep networking alive across unrelated activities.
5. Explain the maintenance cost of new dependencies, settings, and failure
   paths. Reuse the existing input, i18n, HAL, and activity lifecycle rules.

For an out-of-scope proposal, explain the specific conflict with `SCOPE.md` and
suggest a smaller alternative. Preserve the user's explicit task and scope
choices; do not silently replace their feature with another project.

## Settings and activities

A setting needs persistence, validation, translation, rendering, and migration
consideration. Add one when the user needs a choice, not just because a value
could be configurable. A new activity adds code and lifecycle responsibilities;
its actual resident RAM depends on its implementation and must be checked,
not assumed to be permanently allocated.

## Self-review

- [ ] The use case fits CrossMux's reading core or lightweight extensions.
- [ ] Existing mechanisms were checked before adding surface.
- [ ] RAM, largest-block, Flash, power, and maintenance costs are explained.
- [ ] Resources and tasks have explicit lifetimes and failure cleanup.
- [ ] Claims distinguish measured behavior from estimates and simulator checks.
