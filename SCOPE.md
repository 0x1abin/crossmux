# CrossMux Vision & Scope

CrossMux is a community fork of CrossPoint Reader for ESP32 e-ink devices.
Reading comes first: reliable book rendering, legible typography, and responsive
navigation remain the core. Lightweight apps, reading analytics, standby faces,
and on-demand services are also part of this fork.

## What belongs here

- **Reading and library workflows:** EPUB/TXT rendering, typography, fonts,
  dictionaries, bookmarks, progress, local book management, and accessibility.
- **Reading analytics:** statistics, heatmaps, profiles, and achievements.
- **Lightweight apps and customization:** games, small tools, AirPage image
  delivery, themes, clocks, and almanac faces suited to e-ink interaction.
- **On-demand connectivity:** file transfer, OPDS, Calibre, progress sync,
  WeRead, content downloads, and OTA. Existing foreground live modes are
  supported; their connection lifetime and idle-sleep behavior must be explicit.
- **Device support and engineering:** HAL/SDK integration, simulators, tests,
  and changes that improve memory use, Flash headroom, stability, or maintenance.

These categories permit proposals for similar extensions; they do not freeze
CrossMux at its current app list. An upstream feature freeze or a feature's
presence in another fork is not by itself a reason to reject it here.

## The acceptance test

Before adding an activity, setting, service, or dependency, explain:

1. **User benefit:** which reading or lightweight e-ink use case it serves.
2. **Resource cost:** steady-state and peak RAM, largest required heap block,
   Flash/OTA-slot headroom, and persistent storage. Shared code must still fit
   the ESP32-C3 baseline of about 380KB usable RAM without PSRAM. S3 resources
   are target-specific, not extra memory available to every device.
3. **Power and lifetime:** when networking or tasks start and stop, how idle
   sleep works, and what is released when leaving the activity.
4. **Maintenance:** whether an existing activity, helper, setting, or SD asset
   already solves the need; what new dependencies and failure modes it adds.

Measure resource claims where possible and label unmeasured costs as estimates.
A successful compile or simulator run does not establish hardware stability,
battery life, or display quality. Include device verification appropriate to
the change, and state remaining acceptance work.

## Limits

CrossMux is not a general-purpose tablet. Full web browsing, general writing or
media suites, and PDF rendering remain outside the current project scope.
Unbounded background networking, persistent workloads that prevent normal
sleep, and features that compromise the reader's resource budget are also out.
An on-demand connector or small app is evaluated by the acceptance test above,
not rejected solely because it is networked or is not a reader screen.

Keep device-specific behavior behind the HAL/SDK, and apply the same memory,
input, translation, and lifecycle rules to apps as to the reader. See
[AGENTS.md](./AGENTS.md) and the [engineering reference](./docs/engineering/index.md).

## Proposing changes

Use [CrossMux Issues](https://github.com/0x1abin/crossmux/issues) to discuss a
substantial addition before investing in it. Keep proposals concrete: intended
behavior, device targets, resource tradeoffs, and verification. Small fixes and
documentation improvements can go directly through the
[contribution workflow](./docs/contributing/development-workflow.md).
