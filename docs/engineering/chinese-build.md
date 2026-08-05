# Chinese Build (ENABLE_CHINESE_VERSION)

> Deep reference for [CLAUDE.md](../../CLAUDE.md). Self-contained guide to the
> `gh_release_cn` Simplified-Chinese firmware: how the flag gates resources, the
> flash budget, and how to regenerate the embedded CJK fonts.

A dedicated build env, `gh_release_cn`, produces a Simplified-Chinese-only
firmware. The `-DENABLE_CHINESE_VERSION` flag in [platformio.ini](../../platformio.ini)
gates every CN-only resource:

| Resource | Behavior under ENABLE_CHINESE_VERSION |
|---|---|
| i18n string table (`gen_i18n.py`) | Pre-script auto-detects the flag via `env.subst("$BUILD_FLAGS")` and emits **only EN + ZH_CN** into `I18nStrings.cpp` (saves ~144 KB vs the full 23-language table). Detection logs `[gen_i18n] ENABLE_CHINESE_VERSION detected …` during the build. |
| Built-in fonts ([lib/EpdFont/builtinFonts/all.h](../../lib/EpdFont/builtinFonts/all.h)) | Latin headers are skipped. The reader UI exposes one built-in **Noto Sans** because the legacy Serif/Sans IDs both reference the same six per-size CJK headers (`notosans_cjk_{8,10,12,14,16,18}.h`). The old IDs remain valid for settings compatibility. The headers use raw 2-bit bitmaps. **Character coverage is tiered by point size**: 8/10/12pt carry all 3500 chars from `chars_3500_common.txt` plus every CJK glyph found in `chinese.yaml` and feature-specific require-from files (3517 CJK glyphs total); 14/16/18pt carry only the require-from set (747 CJK glyphs). Every size also includes the standard ASCII, Latin-1, and CJK punctuation ranges. The 14/16/18pt sizes rely on a complete SD-card font for broad Chinese EPUB coverage while still rendering every built-in Chinese UI string. |
| Downloadable fonts | The Chinese build uses the same manifest-v1 font manager as global builds; only its catalog URL points to an externally maintained Gitee release. Its embedded 8/10/12pt fonts already cover the Simplified-Chinese UI, so it keeps only the selected reader-size SD font resident and does not load the global build's three size-matched UI fallbacks. This preserves contiguous heap for EPUB image decoding and glyph prewarm. |
| `src/main.cpp` font globals | Each Latin `EpdFont`/`EpdFontFamily` global is aliased to the matching-size CJK header. Bold/italic variants all point at the Regular OTF (no style data in the subset). SD-card fonts still provide style variants when the user loads them. |
| EPUB layout ([lib/Epub/Epub/ParsedText.cpp](../../lib/Epub/Epub/ParsedText.cpp)) | All firmware flavors use the same Unicode-aware CJK splitting, punctuation, line-breaking, and source-space rules. The CN build changes the bundled font data and metrics, not tokenization or inter-word spacing behavior. |
| Activities (`src/activities/apps/chinese-chess/`) | Compiled in (also gated by `build_src_filter +<activities/apps/chinese-chess/>`). |
| WeRead (`src/activities/apps/weread/`) | Compiled into native CN builds through `build_src_filter`. Device requests use the app-local `WeReadHttpClient` over wolfSSL; the native simulator uses the pinned simulator fork's `esp_http_client` shim over the host `curl`. |
| First-boot locale defaults (`src/CrossPointSettings.h`) | A fresh CN build starts with `Language::ZH_CN` and UTC+8 (`clockUtcOffsetQ = 80`); non-CN builds default to `Language::EN` and UTC+0. Saved UTC offsets are preserved across reflashes. When switching CN → global, the existing SKU/renderability guard resets Chinese to English, including legacy settings without a `langSku` marker. |

**Flash budget** (default `partitions.csv`, dual A/B app slot = 6.25 MB):

| Section | Bytes |
|---|---|
| Code + non-font data | 3,364,023 |
| 3 CJK font headers 8/10/12pt (3517 CJK + 497 ASCII/Latin/punctuation glyphs) | 1,427,822 |
| 3 CJK font headers 14/16/18pt (747 CJK + 497 ASCII/Latin/punctuation glyphs) | 786,336 |
| **Total (`gh_release_cn`, 2026-08-06)** | **5,578,181 / 6,553,600 bytes**, 975,419 bytes headroom; 51,860 bytes static RAM |
| **Total (`gh_release_cn_rc`, projected from release delta)** | **5,578,195 / 6,553,600 bytes**, 975,405 bytes headroom; 51,860 bytes static RAM |

A/B OTA rollback works exactly like the Latin build — the firmware fits in
both app slots, and a failed update can auto-revert.

> **Historical note (2026-05-19)**: the committed `notosans_cjk_*.h` headers
> had drifted from `cn_common_chars.txt` in earlier PRs — the headers held
> ~5000 CJK glyphs per size (including ~2000 Traditional Chinese variants
> the project never renders) while `cn_common_chars.txt` declared only the
> 3500 SC chars. A regen during the 老黄历 PR brought them back in sync at
> 3500 SC chars, freeing ~755 KB of dead-weight bitmap data (Flash dropped
> from ~91% to ~79%).
>
> **Update (2026-05-26)**: 14pt was promoted to the 7000 通用汉字 tier with
> extended symbol coverage (number forms / arrows / math / box drawing /
> geometric shapes / dingbats) so the reader-default MEDIUM size renders
> rare names, place names, and modern EPUB symbols. Flash returned to ~92%
> deliberately; the headroom is still ~500 KB. 8/10/12pt and 16/18pt sizes
> were intentionally left at their existing coverage to keep the budget
> in check.
>
> **Update (2026-07-24)**: 14pt returned to the common tier and the extended
> symbol ranges were removed. The common tier now preserves the complete
> 3500-char source pool before adding all required glyphs; the i18n tier now
> contains every CJK glyph scanned from `chinese.yaml` and feature files.
> Relative to the 6,502,659-byte RC baseline, the RC image is 736,664 bytes
> smaller while adding the previously missing required glyphs.
>
> **Update (2026-08-06)**: 14pt moved to the i18n-only tier. Its 747 required
> CJK glyphs keep every built-in UI and feature string renderable, while broad
> Chinese EPUB coverage now uses the downloadable SD-card font. This removes
> 587,322 bytes of raw bitmap and glyph metadata from the firmware. Regeneration
> also picked up 14 UI glyphs that had been added since the previous font build.

The CN build keeps LTO disabled. Combining LTO with the custom ESP-IDF core
rebuild previously allowed cache-critical SPI flash helpers to be linked into
Flash; calling them while cache was disabled caused an early-boot Cache Error.
Do not re-enable LTO without checking the final ELF and map for the affected
IRAM mappings.

The remaining size strategy is **per-size CJK character coverage** in
`build-cn-builtin-fonts.sh` (see "Regenerating the CJK fonts" below).
8/10/12pt carry the complete 3500-char pool plus required glyphs; 14/16/18pt
carry only required glyphs. The smaller tiers shrink raw Flash-resident bitmap
and glyph metadata without adding a decompression buffer, heap allocation, or
DRAM use.

## WeRead transport

WeRead owns its transport in `src/activities/apps/weread/WeReadHttpClient.*`;
the public `HttpDownloader` has no WeRead-specific session, Cookie, or protocol
logic. On device, the client uses wolfSSL through `freeink::SecureClient`, a
caller-owned 4 KB work buffer, and one best-effort persistent connection.
Chapter images are streamed to SD and scheduled in batches by HTTPS host, so
requests for the same host reuse that single connection. Redirects may change
the active host, but only `weread.qq.com` and its subdomains receive the WeRead
Cookie. The connection closes before EPUB packaging, and also closes early
when free heap falls below 20 KB or the largest block falls below 8 KB.

Device requests call `setInsecure()`: traffic is encrypted, but the CA and host
identity are not verified. This makes session credentials and downloaded
content vulnerable to a man-in-the-middle attacker. The native simulator takes
a different path, `WeReadHttpClient -> simulator esp_http_client shim -> curl`,
and uses the host trust store for certificate verification. WeRead uses an
unofficial Web protocol and may stop working when the service changes.

## WeRead progress sync

The reader exposes one **Sync Progress** menu action. When the current EPUB path
exactly matches a standard book in `shelf.bin`, the action uses WeRead; all other
EPUBs continue to use KOReader. WeRead public-account books (`MP_WXS_`), moved
files, and renamed files are intentionally treated as ordinary EPUBs. The
existing long-press KOReader shortcut is unchanged.

Manual WeRead sync renews the saved Cookie, fetches the remote position, and
maps it through the `WRT2` chapter word counts. Positions within two percentage
points are left unchanged. Otherwise the farther position wins automatically:
a remote position is applied locally, or the local position is sent with an
enter/report pair and `rt=0`. The firmware does not synthesize or report reading
time. An expired session directs the user back to **Apps → WeRead** to sign in;
it does not open a QR flow from the reader.

For a new standard-book cache, the downloader fetches cloud progress after the
`WRT2` catalog and before any chapter or image. This request is best effort:
network, authentication, and protocol failures are logged but do not block the
book download. After the EPUB is atomically replaced, a successful result is
stored as the one-shot `WRP1` initial position. The reader consumes it only when
there is no valid local `progress.bin`; an existing local position always wins.
Public-account books skip this prefetch. See [file-formats.md](../file-formats.md)
for the `WRT2` and `WRP1` layouts and migration rules.

## Regenerating the CJK fonts

```bash
# 1. (One-time) install build deps into a venv
python3 -m venv /tmp/cn_font_venv
/tmp/cn_font_venv/bin/pip install -r lib/EpdFont/scripts/requirements.txt

# 2. Place the official Noto Sans CJK SC Regular OTF into the source dir
#    (gitignored) under the filename expected by the script.
#    Source:
#    https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf
cp /path/to/NotoSansSC-Regular.otf lib/EpdFont/builtinFonts/source/NotoSansSC/

# 3. (Optional) regenerate the character lists. build-cn-builtin-fonts.sh
#    calls this for you in Step 0 unless SKIP_CHARSET=1 is set.
#    Writes BOTH cn_common_chars.txt (top-N + require-from) and
#    cn_i18n_chars.txt (require-from only — drives the 14/16/18pt subset).
PYTHON=/tmp/cn_font_venv/bin/python3 \
  python3 lib/EpdFont/scripts/build_cn_charset.py \
    --require-from lib/I18n/translations/chinese.yaml

# 4. Generate the 6 per-size CJK headers
PYTHON=/tmp/cn_font_venv/bin/python3 \
  bash lib/EpdFont/scripts/build-cn-builtin-fonts.sh

# 5. Build
pio run -e gh_release_cn
```

The committed headers were regenerated with source SHA-256
`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`.
Verify a replacement source before regeneration: on the current committed
charset it must reproduce the existing glyph metrics and bitmap bytes. The
subset step deliberately excludes zero-width compatibility code point U+FFA0,
which is present in the official OTF but was absent from the previous source.

`build_cn_charset.py --top N` means "keep the top N characters from the base
pool, then add every required character." Required glyphs never displace base
glyphs, so output can contain more than N characters. `--top` may equal the
pool size but may not exceed it. The script rereads both generated files and
fails if a selected base or required character is missing.

## Force-including feature-specific glyphs

Each feature that needs CJK glyphs not already covered by the natural source
for the point size where it renders (the 3500-char pool at 8–12pt, or
`chinese.yaml` at 14/16/18pt) ships a small dedicated text file and adds it to
the `REQUIRE_FROM` array in
[build-cn-builtin-fonts.sh](../../lib/EpdFont/scripts/build-cn-builtin-fonts.sh).
`build_cn_charset.py --require-from <file>` regex-scans the complete file for
every CJK Unified Ideograph (`[一-鿿]`) and force-includes them in **both**
`cn_common_chars.txt` (8/10/12pt) and `cn_i18n_chars.txt` (14/16/18pt), so
glyphs added via this mechanism render at every CN font size.

Current example: [cn_almanac_chars.txt](../../lib/EpdFont/scripts/cn_almanac_chars.txt)
holds the extra chars `ChineseAlmanac.cpp` / `ChineseCalendarFace.cpp` need
for its ≤14pt rows and 18pt lunar row:

```text
戊庚壬癸寅卯巳酉戌廿蛰闰初七八九十冬腊
```

Pattern when adding a new feature:

1. Create `lib/EpdFont/scripts/cn_<feature>_chars.txt` containing only the
   CJK chars the feature renders that aren't otherwise covered (one line,
   UTF-8, no separators required).
2. Append that path to `REQUIRE_FROM=(... cn_<feature>_chars.txt)` in
   `build-cn-builtin-fonts.sh`.
3. Re-run `bash build-cn-builtin-fonts.sh` and commit the regenerated
   `cn_common_chars.txt`, `cn_i18n_chars.txt`, and six `notosans_cjk_*.h`.

Anti-pattern (don't do this): hiding chars in a `#` YAML comment inside
`chinese.yaml` to abuse the regex scanner. It works (build_cn_charset.py
does scan comments) but couples a font detail to the i18n file's structure
and forces `gen_i18n.py` to know about it. Dedicated `cn_*_chars.txt`
files are self-documenting and orthogonal to i18n.

## Expanding character coverage

`--top` is capped by the source pool size (`chars_3500_common.txt` has 3500
chars by construction — it is the vendored 教育部《现代汉语常用字表》).
Passing `--top 3500` keeps the complete pool; passing `--top 5000` fails.

To enlarge the renderable character set:

1. **For a handful of specific chars**: add a feature-scoped
   `cn_<feature>_chars.txt` as described above. One-line file + one-line
   append to `REQUIRE_FROM`. No pool expansion needed.
2. **For broad coverage gain** (e.g. classical literature, GB2312 Lv2): drop a
   larger source list into `lib/EpdFont/scripts/` (e.g. a `gb2312_full.txt`
   union of Lv1+Lv2) and point `SOURCE_FILE` in `build_cn_charset.py` at it.
   This is a deliberate Flash-budget decision — every 1000 extra chars adds
   roughly **380 KB** to the 8/10/12pt headers combined.

The hard ceiling is the 6.25 MiB A/B-OTA slot. The 2026-08-06 measured
headroom is 975,419 bytes, so broad pool expansion must be budgeted and
measured rather than inferred from the source character count.

## Complete Chinese SD fonts

The Chinese build downloads its catalog from:

```text
https://gitee.com/x1abin/crossmux-fonts/releases/download/sd-fonts-m<manifest>-b<binary>/fonts.json
```

The version numbers come from the firmware's manifest and cpfont constants.
The external Gitee service must keep manifest v1 and all referenced cpfont v4
assets under that tag. `baseUrl`, family/file names, non-zero file sizes, and
CRC32 values must be valid; the external catalog maintainer is responsible for
complete Chinese coverage. Global firmware continues to use the corresponding
GitHub service.

The firmware offers the guided downloader when a built-in font is changed to
14/16/18pt, or when an EPUB scan sees a missing U+4E00–U+9FFF or
U+F900–U+FAFF glyph. The render task only records the first codepoint; the main
loop opens the activity. A reader session prompts once, and an active SD font
is never rechecked. After confirmation it opens the ordinary **Manage Fonts**
flow, preserving install/update/delete and batch-download behavior. A single
download selects that family and opens the text preview; **Download All**
prefers the stable `NotoSansSC` family, while **Update All** preserves the
current selection. Preview changes read from SD without rebuilding the Flash
cache. Leaving Text Settings caches only the final family and point size.

When `NotoSansSC` is discovered through download, web upload, or an SD-card
copy, it replaces the active built-in face and the built-in row is hidden. If
the family disappears, the existing missing-font fallback clears the SD
selection and the single built-in Noto Sans row becomes visible again. Legacy
Chinese settings that selected the duplicate built-in Serif ID migrate to the
Sans ID. Font sources and catalog-generation configuration remain outside this
repository.

## Known limitations

- **No bold/italic CJK glyphs**: the bitmaps come from a single NotoSansSC-Regular subset. UI elements that pass `EpdFontFamily::Style::Bold` render the regular weight under CN.
- **Font-size dropdown affects rendered size**: each reader size (12/14/16/18pt) and UI size (10/12pt) and small font (8pt) has its own bitmap header. Switching size really does swap glyph bitmaps.
- **Rare characters render as □ in reader at SMALL until an SD font is installed**: the 12pt bitmap uses the complete 3500-char pool plus required glyphs. It omits classical/scientific rarities, Traditional Chinese variants, and uncommon names outside that set; for example, `璟` is intentionally absent. The reader offers the complete-font downloader on the first detected missing glyph.
- **CJK in reader at MEDIUM/LARGE/EXTRA_LARGE needs an SD font for full coverage**: the built-in 14/16/18pt subsets contain only required UI glyphs. Selecting any of these sizes offers the same downloader before a Chinese EPUB encounters missing text.
- **`FontDecompressor` is bypassed for CJK** by design — bitmaps are stored raw because compressing 6 fonts × ~50 KB groups fragments the heap on boot.
- **No Traditional Chinese support**: the build is explicitly SC-only (`_language_code: ZH_CN`, no `zh-TW`/`zh-HK` yaml). TC glyphs are not in any pool; TC strings would render as missing-glyph placeholders.

## Files

| Path | Role |
|---|---|
| `lib/EpdFont/scripts/build_cn_charset.py` | Rank the base pool by wordfreq Zipf, preserve its top-N, then add all required glyphs. Pool capped at `chars_3500_common.txt` size (3500). |
| `lib/EpdFont/scripts/chars_3500_common.txt` | Source pool — 现代汉语常用字表, 3500 chars (committed). To expand coverage, swap this file (see "Expanding character coverage"). |
| `lib/EpdFont/scripts/cn_common_chars.txt` | Generated common subset, drives 8/10/12pt (committed). Contains all 3500 base chars plus required additions; currently 3517 unique CJK chars. |
| `lib/EpdFont/scripts/cn_i18n_chars.txt` | Generated i18n-only subset, drives 14/16/18pt (committed). Contains every CJK char found in `--require-from` inputs. |
| `lib/EpdFont/scripts/build-cn-builtin-fonts.sh` | pyftsubset → fontconvert.py pipeline, six headers. Default re-runs `build_cn_charset.py`; set `SKIP_CHARSET=1` to reuse the current `cn_common_chars.txt`. The `REQUIRE_FROM=(...)` array at the top lists every file scanned for force-included CJK chars — add new feature-scoped `cn_*_chars.txt` files here. |
| `lib/EpdFont/scripts/cn_almanac_chars.txt` | Feature-scoped force-include for `ChineseAlmanac.cpp` / `ChineseCalendarFace.cpp` — ganzhi stems/branches missing from the common tier plus lunar-row vocabulary missing from the i18n tier. Single-line UTF-8. |
| `lib/EpdFont/builtinFonts/notosans_cjk_{8,10,12,14,16,18}.h` | Generated raw 2-bit bitmap headers (committed). Must match `cn_common_chars.txt` (8/10/12pt) and `cn_i18n_chars.txt` (14/16/18pt) — see consistency check below. |
| `lib/EpdFont/builtinFonts/source/NotoSansSC/` | OTF source dir (gitignored except for `.gitignore`). Drop `NotoSansSC-Regular.otf` here. |
| `lib/I18n/translations/chinese.yaml` | Simplified Chinese translations (`_language_code: ZH_CN`); also fed to `--require-from` so every CJK char in `STR_*: "value"` lines is forced into both subsets. Do **not** hide font-only chars in `#` comments — use a `cn_<feature>_chars.txt` file instead. |

## Consistency check

After regenerating, confirm the character lists and bitmap headers match:

- `cn_common_chars.txt` has 3517 unique CJK glyphs and contains the complete
  3500-char base pool.
- `cn_i18n_chars.txt` has 747 unique CJK glyphs and contains every glyph
  scanned from `chinese.yaml` and feature-specific files.
- 8/10/12pt each contain 4014 glyphs; 14/16/18pt each contain 1244 glyphs.
- Every generated header says `mode: 2-bit`.

```bash
python3 -c "
import re
from pathlib import Path
root = Path('.')
cjk = lambda s: {c for c in s if '\u4e00' <= c <= '\u9fff'}
scripts = root / 'lib/EpdFont/scripts'
pool = cjk((scripts / 'chars_3500_common.txt').read_text())
common = cjk((scripts / 'cn_common_chars.txt').read_text())
i18n = cjk((scripts / 'cn_i18n_chars.txt').read_text())
required = cjk((root / 'lib/I18n/translations/chinese.yaml').read_text())
required |= cjk((scripts / 'cn_almanac_chars.txt').read_text())
assert (len(pool), len(common), len(i18n)) == (3500, 3517, 747)
assert common == pool | required and i18n == required
for size, expected in [(8, 4014), (10, 4014), (12, 4014),
                       (14, 1244), (16, 1244), (18, 1244)]:
    header = (root / f'lib/EpdFont/builtinFonts/notosans_cjk_{size}.h').read_text()
    intervals = re.search(r'Intervals\[\] = \{(.*?)\n\};', header, re.S).group(1)
    codepoints = set()
    for first, last in re.findall(r'\{\s*(0x[0-9A-F]+),\s*(0x[0-9A-F]+),', intervals):
        codepoints.update(range(int(first, 16), int(last, 16) + 1))
    expected_cjk = common if size <= 12 else i18n
    assert {ord(char) for char in expected_cjk} <= codepoints
    assert 'mode: 2-bit' in header
    assert '\n    true,\n    nullptr,\n' in header  # EpdFontData::is2Bit
    assert len(codepoints) == expected, (size, len(codepoints))
    if size == 14:
        assert not {ord(char) for char in common - i18n} & codepoints
print('Chinese font coverage OK')
"
```

Any assertion failure means a list or header is stale; re-run the regeneration
steps before committing.
