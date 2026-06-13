# Cache Management & Invalidation

> Deep reference for [CLAUDE.md](../../CLAUDE.md). The SD-card cache trades flash
> for RAM/CPU. **Always bump the format version BEFORE changing a binary layout.**
> For the byte-level binary formats themselves, see
> [../file-formats.md](../file-formats.md) (canonical reference).

## Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, sections/*.bin}`

**Hash**: `std::hash<std::string>{}(filepath)` → Moving/renaming file = new hash = lost progress

## Cache Invalidation Rules

**Cache is automatically invalidated when**:
1. **File format version changes** (see [../file-formats.md](../file-formats.md))
   - `book.bin` version number incremented
   - `section.bin` version number incremented
2. **Render settings change**:
   - Font family or size (`SETTINGS.fontFamily`, `SETTINGS.fontSize`)
   - Line spacing (`SETTINGS.lineSpacing`)
   - Paragraph spacing (`SETTINGS.extraParagraphSpacing`)
   - Screen margins (`SETTINGS.screenMargin`)
3. **Viewport dimensions change**:
   - Screen orientation change
   - Display resolution change
4. **Book file modified**:
   - Moved, renamed, or content changed (new hash)

**Manual Cache Clear** (safe operations):
```bash
# Delete ALL caches (forces full regeneration)
rm -rf /path/to/sd/.crosspoint/

# Delete specific book cache
rm -rf /path/to/sd/.crosspoint/epub_<hash>/

# Keep progress, delete only rendered sections
rm -rf /path/to/sd/.crosspoint/epub_<hash>/sections/
```

**When to Clear Cache**:
- EPUB parsing errors after code changes to `lib/Epub/`
- Corrupt rendering (missing text, wrong layout)
- Testing cache generation logic
- After modifying:
  - `lib/Epub/Epub/Section.cpp`
  - `lib/Epub/Epub/BookMetadataCache.cpp`
  - Render settings in `CrossPointSettings`

## Cache File Format Versioning

**Source**: `lib/Epub/Epub/Section.cpp`, `lib/Epub/Epub/BookMetadataCache.cpp`

**Current Versions** (as of [../file-formats.md](../file-formats.md)):
- `book.bin`: **Version 8** (metadata structure) — bumped on the upstream-master sync; the fork shipped 6 and upstream shipped 7 with an identical layout (both added the `language` field), so 8 sits above both lineages and forces a one-time clean re-parse.
- `section.bin`: **per-flavor** — Latin builds **Version 30**, Chinese builds (`ENABLE_CHINESE_VERSION`) **Version 31**. The two flavors emit different word streams (per-character CJK tokenization), so each carries an independent counter; numbers stay distinct and above every previously-shipped value so a firmware flavor swap never reuses the other's cache.

**Version Increment Rules**:
1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in [../file-formats.md](../file-formats.md)

**Example** (incrementing section format version):
```cpp
// lib/Epub/Epub/Section.cpp
static constexpr uint8_t SECTION_FILE_VERSION = 30;  // bump before any layout change

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```
