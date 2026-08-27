# Cache Management & Invalidation

> Deep reference for [CLAUDE.md](../../CLAUDE.md). The SD-card cache trades flash
> for RAM/CPU. **Always bump the format version BEFORE changing a binary layout.**
> For the byte-level binary formats themselves, see
> [../file-formats.md](../file-formats.md) (canonical reference).

## Cache Structure on SD Card

**Location**: `.crosspoint/` directory on SD card root

**Structure**:
- EPUB: `.crosspoint/epub_<hash>/{book.bin, progress.bin, cover.bmp, thumb_<height>.bmp, sections/*.bin}`
- XTC: `.crosspoint/xtc_<hash>/{cover.bmp, thumb_<height>.bmp}`
- TXT: `.crosspoint/txt_<hash>/{index.bin, chapters.bin, progress.bin}`

`recent.json` stores the theme-neutral `thumb_[HEIGHT].bmp` path template for
EPUB and XTC entries. Home themes replace `[HEIGHT]` with their requested
thumbnail height. Lyra Carousel redirects only its in-memory `RecentBook` copy
to `cover.bmp`, generating that full cover when missing; switching themes must
not replace the persisted template.

When an EPUB has no `book.bin`, thumbnail generation first builds metadata but
continues to skip CSS. Inx resolves each target and compatibility thumbnail at
most once per Activity, displays the existing localized loading popup while it
generates the missing covers under one framebuffer loan, then performs one
final screen refresh. `Missing` is Activity-local, so leaving and reopening the
theme allows a failed cover to be retried.

On `BOARD_HAS_PSRAM` targets, Inx also keeps successfully validated thumbnail
BMP files in an Activity-local PSRAM cache. Entries are capped at 64 KB and the
cache at 512 KB; oversized files, allocation/read failures, and devices without
usable PSRAM continue through the same SD streaming path. The cache owns no new
on-disk format and is released when the Activity exits or its thumbnail height
changes.

TXT `index.bin` version 7 stores a partial or complete lazy page index, the
detected source encoding, and the paragraph-spacing mode. It is invalidated by
file-size, viewport, font, margin, alignment, or paragraph-spacing changes; see
[file-formats.md](../file-formats.md#txt-reader-cache) for its byte layout and
legacy-version compatibility behavior.

TXT `chapters.bin` version 1 is an optional source-offset chapter index built
the first time the chapter list opens. It is independent of pagination and is
invalidated by file size, encoding, or chapter-index version.

TXT `progress.bin` stores the current source offset so a chapter jump can be
restored without extending the page index through all intervening text. Legacy
four-byte page-number records remain readable.

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

Malformed caches are treated as misses. Readers validate every POD/string read,
length and lookup-table boundary before publishing parsed state. A truncated or
oversized `book.bin` is closed and removed; the current call returns no entry,
and the next normal EPUB load rebuilds it. This is a validation change only and
does not alter the version-11 binary layout.

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
- `book.bin`: **Version 11** (metadata structure) — includes NFC-composed titles and ignores ambiguous EPUB guide text references while remaining above every version shipped by either lineage.
- `section.bin`: unified firmware uses **Version 57**. Version 56 was the former
  Latin flavor; selecting 57 invalidates pagination made with its different
  built-in font metrics.

**Version Increment Rules**:
1. **ALWAYS increment version** BEFORE changing binary structure
2. Version mismatch → Cache auto-invalidated and regenerated
3. Document format changes in [../file-formats.md](../file-formats.md)

WeRead caches invalidate independently through their own magic/version or a
versioned filename. Do not add a global generation that recursively clears the
cache during application startup. Chapter XHTML compatibility is paired with
its image-index magic, so a mismatch causes that chapter to be rebuilt when the
user caches the book. Manual cache clearing remains the fallback for disposable
legacy files.

**Example** (incrementing section format version):
```cpp
// lib/Epub/Epub/Section.cpp
static constexpr uint8_t SECTION_FILE_VERSION = 42;  // bump before any layout change

// Add new field to structure
struct PageLine {
  // ... existing fields ...
  uint16_t newField;  // New field added
};
```
