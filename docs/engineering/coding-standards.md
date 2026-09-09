# Coding Standards

> Deep reference for [AGENTS.md](../../AGENTS.md). Naming, header guards, and the
> error-handling philosophy. Memory/RAII rules live in
> [memory-and-allocation.md](memory-and-allocation.md); platform hazards in
> [esp32-pitfalls.md](esp32-pitfalls.md).

## Naming Conventions
* Classes: PascalCase (e.g., EpubReaderActivity)
* Methods/Variables: camelCase (e.g., renderPage())
* Constants: UPPER_SNAKE_CASE (e.g., MAX_BUFFER_SIZE)
* Private Members: memberVariable (no prefix)
* File Names: Match Class names (e.g., EpubReaderActivity.cpp)

## Header Guards
* Use #pragma once for all header files.

## Error Handling Philosophy

**Source**: [src/main.cpp:132-143](../../src/main.cpp), [lib/GfxRenderer/GfxRenderer.cpp:10](../../lib/GfxRenderer/GfxRenderer.cpp)

**Pattern Hierarchy**:
1. **LOG_ERR + return false** (90%): `LOG_ERR("MOD", "Failed: %s", reason); return false;`
2. **LOG_ERR + fallback**: `LOG_ERR("MOD", "Unavailable"); useDefault();`
3. **assert(false)**: Only for fatal "impossible" states (framebuffer missing)
4. **ESP.restart()**: Only for recovery (OTA complete)

**Rules**: NO exceptions, NO abort(), ALWAYS log before error return

> The full allocation-related error-handling guidance (OOM, `makeUniqueNoThrow`)
> lives in [memory-and-allocation.md](memory-and-allocation.md).
