# SD Card Fonts

CrossPoint supports loading additional fonts from the SD card, including fonts
with extended Unicode coverage (CJK, Cyrillic, Greek, etc.).

## Installing Fonts

There are three ways to install fonts:

### Option 1: Download from device (recommended)

1. Connect your CrossPoint reader to Wi-Fi
2. Go to **Settings > Reader > Manage Fonts**
3. Browse available font families and tap to download
4. A single download selects that family and opens **Text Settings** for
   preview. **Download All** prefers `NotoSansSC` (or the first valid catalog
   family when it is absent); **Update All** keeps the current selection.
   Each operation uses one byte-weighted progress bar across all of its font
   files and refreshes it after at least 10 percentage points of progress.
5. Preview any family or size you want. Leaving **Text Settings** caches only
   the final SD font and size in internal Flash. The preprocessing page appears
   because a download marks the selected font as changed; ordinary layout or
   style edits do not trigger it.

### Option 2: Upload via web browser

1. Start **File Transfer** and connect through **Join Network** or **Create Hotspot**
2. Open the web interface URL shown on the reader
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 3: Manual SD card copy

1. Download font files from the
   [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts)
2. Copy font family folders to one of two locations on your SD card:

   - `/.fonts/` — hidden directory (preferred; keeps the SD root tidy
     when mounted on a desktop)
   - `/fonts/` — visible directory (use this if your OS hides dot-files
     and you'd rather see the folder in your file manager)

   Both roots are always scanned at boot and the results are merged: a
   family installed in `/fonts/` shows up even when `/.fonts/` also
   exists, and vice versa. The two roots only collide if the same family
   name appears in both — in that case the copy in `/.fonts/` wins and
   the duplicate in `/fonts/` is ignored.

       SD Card Root/
       ├── .fonts/                     ← Hidden root (preferred)
       │   └── Literata/
       │       ├── Literata_12.cpfont
       │       ├── Literata_14.cpfont
       │       ├── Literata_16.cpfont
       │       └── Literata_18.cpfont
       └── fonts/                      ← Visible root (equally valid)
           └── Merriweather/
               ├── Merriweather_12.cpfont
               └── ...

3. Insert the SD card and power on your CrossPoint reader

## CJK in the User Interface

Unified firmware registers embedded Simplified-Chinese 8/10/12pt subsets as
fallbacks for the compact international UI fonts. No extra SD UI sizes stay
resident, preserving contiguous heap. Japanese, Korean, Traditional Chinese,
and uncommon Han glyphs outside those subsets may still show replacement boxes.

Reader content uses only the selected reader-size `.cpfont` at runtime. The
built-in 12pt subset is an offline fallback; install a complete CJK family for
broader coverage, other point sizes, and style variants.

## Available Pre-Built Fonts

The current list of pre-built fonts is maintained in the
[crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts).

Unified firmware loads the manifest through `crossmux.cn` for China profiles
and `crossmux.com` for Global profiles; it does not select GitHub or Gitee
directly. Both catalogs publish manifest v1 with a valid
`baseUrl`, family/file names, non-zero file sizes, and CRC32 values. Every
referenced asset must be cpfont v4 and provide the coverage promised by its
catalog maintainer. Font sources and catalog-generation configuration are not
kept in this repository.

In a Chinese EPUB, confirming the incomplete-font prompt starts an automatic
reader-only flow: connect Wi-Fi, download and verify the complete `NotoSansSC`
family, and save the exact current point size. It then silently restarts before
loading the font, avoiding the fragmented heap left by Wi-Fi/TLS, and preprocesses
that size on the clean boot before returning to the same book and visible text
position. Page numbers can change because the new font repaginates the book. Back
or Wi-Fi cancellation returns to the book without changing the selection, while
Home always returns to Home. Download or clean-boot font-load errors offer Retry
and Back. A preprocessing failure keeps the verified SD font, disables Flash
acceleration, and returns after one notice.

The manual font manager and the prompt opened from Text Settings keep the
interactive preview flow described above.

## Converting Custom Fonts

To convert your own TrueType/OpenType fonts:

### Prerequisites

    pip install freetype-py fonttools

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

**Text Settings > Style > Synthetic Bold** replaces EPUB bold faces with the
regular or italic face thickened by one (Light), two (Standard), or three
(Heavy) horizontal pixels at render time. It does not change font metrics,
wrapping, or the `.cpfont` format, and it does not affect menus or status-bar
text. Standard is the default; existing Standard and Heavy choices retain their
names and move to the stronger two- and three-pixel effects on upgrade.

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020–U+007E (Basic Latin) |
| `latin1` | U+0080–U+00FF (Latin-1 Supplement) |
| `latin-ext` | European languages (Latin + Extended-A/B + punctuation + ligatures) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `hebrew` | Hebrew + Alphabetic Presentation Forms |
| `arabic` | Arabic + Supplement + Extended-A + Presentation Forms A/B (RTL, contextual shaping) |
| `georgian` | Georgian + Georgian Supplement |
| `armenian` | Armenian |
| `ethiopic` | Ethiopic + Extended |
| `vietnamese` | Vietnamese subset (ơ/ư and combining marks) |
| `ipa-chars` | IPA Extensions + Spacing Modifier Letters (phonetic transcription) |
| `punctuation` | General punctuation (U+2000–U+206F) |
| `cjk` | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth |
| `hangul` | Korean Hangul syllables + Jamo + Compatibility Jamo |
| `cherokee` | Cherokee (historic + supplement block) |
| `tifinagh` | Tifinagh |
| `symbols` | Math, currency, arrows, box-drawing, misc symbols, dingbats |
| `reading` | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin` | Matches the firmware's built-in font conversion intervals |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` — force FreeType's auto-hinter instead of the font's native hinting (useful when a font's built-in hints produce poor results at small sizes).

Install custom fonts via the web interface or manual SD card copy.
