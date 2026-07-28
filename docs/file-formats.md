# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 11

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

> Version 9 forced a one-time rebuild after upstream began NFC-composing titles.
> Upstream version 10 ignores ambiguous EPUB guide text references. CrossMux uses
> 11 to include both changes while remaining above every shipped value from
> either lineage. `BookMetadataCache.cpp` is the source of truth.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 11
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Versions 46 / 47

> Chinese builds (`ENABLE_CHINESE_VERSION`) carry an independent version counter,
> currently **47**; Latin builds use **46**. The byte layout is identical between
> flavors; only
> the word-stream contents differ (per-character CJK tokenization), so caches are
> not reusable across flavors.
>
> Versions 34/35 introduced the flat TextBlock arena layout. Versions 36/37
> invalidated cached word positions after Arabic contextual shaping began measuring
> shaped visual text. Versions 38/39 add the upstream line-through style and
> resumable partial-build cache changes. Versions 40/41 keep the same byte layout
> but invalidate pagination because compressed line heights are rounded instead
> of truncated. Versions 42/43 persist each image's book-internal source href for
> lazy extraction and serialize ruby annotations/group-continuation styles.
> Versions 44/45 invalidate pagination after closed HTML tags began splitting
> adjacent text blocks. Versions 46/47 keep the byte layout unchanged but
> invalidate pagination because oversized tokens now wrap at UTF-8 boundaries
> without inserting synthetic hyphens. The counters remain
> distinct and above every previously shipped value so a firmware-flavor swap
> cannot read the other flavor's stale cache. `lib/Epub/Epub/Section.cpp` is the
> source of truth.

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs used by KOReader sync page refinement
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript, plus the internal ruby-group continuation marker
- optional ruby annotation strings for `<ruby>` / `<rt>` content
- image source hrefs used for lazy extraction
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 46
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 96

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## WeRead cache

The Simplified Chinese build keeps WeRead's private data below `/.crosspoint/weread/`.
Completed EPUB files are stored in `/WeRead/`; the pre-release `/Books/WeRead/`
location is not migrated or read.

- `session.bin` is the existing device-bound obfuscation envelope. Its decoded
  payload starts with `WRA1\n`, followed by bounded `wr_vid`, `wr_skey`, and
  `wr_rt` lines. No response body, signature, or complete Cookie header is
  stored.
- `shelf.bin`, `<bookId>/toc.bin`, per-chapter
  `<bookId>/chapters/NNNNNN.images`, and transient `<bookId>/images.work`
  indexes start with a 12-byte little-endian header:
  `uint32 magic`, `uint16 version`, `uint16 recordSize`, `uint32 recordCount`.
  Their magic values are `WRS5` (`0x35535257`) and `WRT1` (`0x31545257`);
  image indexes use `WRI1` (`0x31495257`) and image work indexes use `WIP1`
  (`0x31504957`). Version is currently `1`.
- Shelf records contain fixed `bookId[64]`, `title[192]`, and `author[96]`
  fields followed by `uint32 readUpdateTime`. TOC records contain fixed
  `chapterUid[64]`, `title[192]`,
  `uint32 chapterIdx`, and a paid flag.
- `WRI1` records contain a generated EPUB-relative `href[64]` and the original
  HTTPS image URL in `url[512]`; both are NUL-terminated. TXT chapters have a
  valid zero-record index. Images that pass download and JPEG/PNG validation
  are stored below `<bookId>/images/` and embedded into the generated EPUB.
- `WIP1` records contain one `WRI1` record followed by `uint8 state`,
  `uint8 attempts`, `uint8 redirects`, and one reserved byte. They schedule
  pending images by HTTPS host so one TLS connection can serve a host batch.
  During an `Embed` download, completed records are also the authoritative
  image list for the OPF manifest and ZIP packaging pass.
  `images.work` is always rebuilt from `WRI1`, may be deleted after interruption,
  and is removed when the job finishes; it is not a durable cache format.
- Per-book `options.bin` is an atomic fixed 8-byte `WRO1` record:
  `uint32 magic` (`0x314F5257`), `uint16 version` (`1`), `uint8 imagePolicy`,
  and one zero reserved byte. Policy `0` embeds validated images and policy `1`
  excludes image requests and EPUB image entries while retaining `WRI1` and
  existing image files. Missing, truncated, unknown-policy, or otherwise
  damaged records fall back to policy `0`. The record is replaced only after a
  generated EPUB succeeds; a directly returned complete EPUB remains unchanged.
- Per-book `detail.bin` starts with a fixed 1024-byte `WBD1` header followed
  immediately by the decoded UTF-8 introduction. The version-1 header contains
  `uint32 magic` (`0x31444257`), `uint16 version`, `uint16 headerSize`,
  `uint32 introLength`, `uint16 newRating`, `uint16 flags`,
  `uint32 newRatingCount`, `uint32 totalWords`, then fixed NUL-terminated
  `title[192]`, `author[96]`, `publisher[96]`, `category[96]`, and
  `coverUrl[512]` fields plus eight zero reserved bytes. Flag bit 0 records that
  the introduction was truncated at its 64 KiB UTF-8-safe limit. The file size
  must equal `1024 + introLength`; unknown flags, non-zero reserved bytes, and
  unterminated fixed strings invalidate the cache. The file is committed from
  `detail.bin.part` only after the complete response parses successfully.
- A successfully converted 96×140 2-bit cover is stored as
  `<bookId>/cover.bmp`. Its validated JPEG/PNG source is retained as
  `<bookId>/cover.source.jpg` or `<bookId>/cover.source.png` and embedded as the
  generated EPUB's `cover-image`; only source `.part` files and `cover.bmp.part`
  are transient. A failed fetch or conversion does not replace an existing BMP.
Readers reject a wrong magic, version, record size, or total file length.
Writers use `.part` plus atomic replacement, so a damaged or interrupted index
is never exposed as current data.

`WRA1` replaces the pre-release `WRD3` session marker after the application
rename. `WRS5` rejects `WRS4` shelves because shelf records now include
`readUpdateTime`; a successful sync rewrites them in descending timestamp
order while preserving server order for ties. Existing per-book detail,
cover, and EPUB files are retained. `WRS4` rejected `WRS3` shelves whose book
titles could be overwritten by nested category titles. A cached chapter
without a valid `WRI1` index is
downloaded again so pre-image-support chapter caches cannot silently lose
figures. Existing `/WeRead/*.epub` files remain readable and are not upgraded
automatically; cache a book again to embed its available cover and selected
chapter images.

## SD-card font cache

For the runtime read path, OTA/rollback lifecycle, rebuild triggers, progress
UI, and performance verification, see
[SD-Card Font Cache](engineering/sd-card-font-cache.md). Its
[partition reuse model](engineering/sd-card-font-cache.md#partition-reuse-model)
shows how the inactive application slot changes roles without modifying
`otadata` or using SPIFFS.

The non-running OTA application slot may temporarily hold the selected SD
reader font. It is a disposable acceleration cache, not a firmware image and
does not modify `otadata`. A normal online or SD-card OTA erases and overwrites
it. The current 0x640000-byte OTA slots reserve the first 4096 bytes for the
cache header, leaving at most 6,549,504 bytes for one `.cpfont` payload.

Header version 1 is a fixed 156-byte little-endian record at slot offset 0:

| Offset | Field |
|---:|---|
| 0 | `uint8 magic[8]` = `CPSDFC1\0` |
| 8 | `uint16 version` = 1 |
| 10 | `uint16 headerSize` = 156 |
| 12 | `uint32 payloadSize` |
| 16 | `uint32 contentHash` (FNV-1a of the `.cpfont` header and style TOC) |
| 20 | `uint32 payloadCrc` |
| 24 | `uint32 headerCrc` (CRC-32 with this field zeroed) |
| 28 | NUL-terminated `char sourcePath[128]` |

The `.cpfont` bytes start at offset 4096. A writer first erases the header,
writes and CRC-verifies the complete payload, then commits the header last.
Startup validates the header, source path and size, content hash, and normal
`.cpfont` structure; it deliberately does not rescan the complete payload CRC.
Any Flash read failure immediately falls back to the SD source.

The legacy `CPOTAF1\0` Magic is rejected even when its header CRC is valid, so
older caches safely fall back to SD and are rebuilt through the normal
preprocessing flow.

After an OTA, application confirmation is deferred until the new firmware has
initialized the display and physically rendered its startup verification page.
Only after confirmation cancels rollback may the old firmware slot be erased
and rebuilt as a font cache. If that copy is interrupted or fails, the
uncommitted header remains invalid and the selected font loads from SD.
