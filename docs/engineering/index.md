# Engineering Reference (Agent System of Record)

This directory is the deep firmware-engineering reference for CrossPoint Reader.
[CLAUDE.md](../../CLAUDE.md) (auto-loaded into every session) is the **map**: it
holds the non-negotiable invariants and points here for detail. Read the file
that matches your task — don't load everything at once.

| Doc | Read this when… |
|---|---|
| [hardware-constraints.md](hardware-constraints.md) | Touching RAM/flash budgets, allocations, `std::vector`/string usage, SPIFFS writes, or detecting the host platform. The Resource Protocol (9 rules) lives here. |
| [memory-and-allocation.md](memory-and-allocation.md) | Allocating heap memory, using `new`/`malloc`/`makeUniqueNoThrow`, smart pointers, or RAII cleanup. |
| [esp32-pitfalls.md](esp32-pitfalls.md) | Writing ISRs, `string_view` at C boundaries, raw buffer casts (RISC-V alignment), templates/`std::function`, or parsing network JSON (ArduinoJson v7). |
| [build-system.md](build-system.md) | Working with PlatformIO, build environments, the critical build flags (DESTRUCTOR_CLOSES_FILE / SINGLE_BUFFER_MODE), or `platformio.local.ini`. |
| [architecture-and-patterns.md](architecture-and-patterns.md) | Using the HAL, singletons, the activity lifecycle, FreeRTOS tasks, or fonts. (Big-picture diagrams: [../contributing/architecture.md](../contributing/architecture.md).) |
| [coding-standards.md](coding-standards.md) | Naming, header guards, or the error-handling pattern hierarchy. |
| [ui-and-input.md](ui-and-input.md) | Rendering UI, handling orientation, mapping buttons, or using the `GUI`/UITheme macro and `tr()`. |
| [generated-files.md](generated-files.md) | Editing HTML pages, i18n translations, or fonts (anything produced by a build script). |
| [testing-and-debugging.md](testing-and-debugging.md) | Building, monitoring, debugging crashes, verifying changes, or reading CI workflows. |
| [git-workflow.md](git-workflow.md) | Any git operation: detecting repo context, branching, commit format, or deciding whether to commit. |
| [cache-management.md](cache-management.md) | Changing a cache binary layout, invalidating caches, or bumping a format version. (Byte-level formats: [../file-formats.md](../file-formats.md).) |
| [sd-card-font-cache.md](sd-card-font-cache.md) | Reviewing the SD-card reader-font cache, its inactive-OTA-slot backend, rollback boundary, commit protocol, SD fallback, progress UI, or performance logs. |
| [chinese-build.md](chinese-build.md) | Working on unified-firmware content profiles or embedded CJK fonts. |
| [firmware-release.md](firmware-release.md) | Changing Nightly targets, packaging, GitHub/COS publishing, regional indexes, rollback, or OTA release contracts. |
| [device-variants.md](device-variants.md) | Building or flashing for the Xteink X3 vs X4, runtime device detection (one binary, both panels), and the per-device hardware differences. |
| [eego-a4.md](eego-a4.md) | Building, first-flashing, recovering, or hardware-validating the experimental ESP32-S3 eego A4 target. |
| [murphy-m4.md](murphy-m4.md) | Building, first-flashing, recovering, or hardware-validating the experimental ESP32-S3 Murphy M4 target. |
| [waveshare-epaper-397.md](waveshare-epaper-397.md) | Building, flashing, or hardware-validating the experimental Waveshare ESP32-S3 ePaper 3.97 target. |
| [upstream-merge-policy.md](upstream-merge-policy.md) | Resolving a sync conflict on `CLAUDE.md` / `.skills/SKILL.md` — how to keep the map thin and route upstream changes into these docs. |

## Related docs outside this directory

- [../contributing/](../contributing/) — human contributor guide (getting started, architecture, workflow, testing).
- [../file-formats.md](../file-formats.md) — canonical binary cache format reference.
- [../i18n.md](../i18n.md) — internationalization system.
- [../webserver-endpoints.md](../webserver-endpoints.md) — web server API reference.
- [../../SCOPE.md](../../SCOPE.md), [../../GOVERNANCE.md](../../GOVERNANCE.md) — project scope and governance guardrails.
