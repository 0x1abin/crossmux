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
| Built-in fonts ([lib/EpdFont/builtinFonts/all.h](../../lib/EpdFont/builtinFonts/all.h)) | Latin headers are skipped. Six per-size CJK headers (`notosans_cjk_{8,10,12,14,16,18}.h`) replace them — raw 2-bit bitmaps. **Character coverage is tiered by point size**: 8/10/12pt carry the small UI subset (~3500 chars from `cn_common_chars.txt` + i18n require-from + ASCII + Latin-1 + CJK punctuation); **14pt carries the large reader-default subset (7000 通用汉字 from `chars_7000_common.txt` + extended symbol ranges — number forms / enclosed alphanumerics / box drawing / block elements / geometric shapes / misc symbols / dingbats — plus the standard ASCII/Latin/punct base)**; 16pt/18pt carry only the i18n require-from CJK subset (~430 chars from `chinese.yaml` + ASCII + Latin-1 + CJK punctuation). The 16/18pt sizes are tuned for reader LARGE/EXTRA_LARGE (intended for English EPUB) while still rendering UI strings; Chinese EPUB text at 16/18pt shows blank for chars outside the i18n subset. |
| `src/main.cpp` font globals | Each Latin `EpdFont`/`EpdFontFamily` global is aliased to the matching-size CJK header. Bold/italic variants all point at the Regular OTF (no style data in the subset). SD-card fonts still provide style variants when the user loads them. |
| EPUB layout ([lib/Epub/Epub/ParsedText.cpp](../../lib/Epub/Epub/ParsedText.cpp)) | CJK punctuation rules are active: line-head prohibition (禁则) glues trailing punctuation back onto the previous line; full-width punctuation gets width-padded so it occupies a full CJK cell. Both are zero-cost in non-CN builds (gated by `#ifdef`). |
| Activities (`src/activities/apps/chinese-chess/`) | Compiled in (also gated by `build_src_filter +<activities/apps/chinese-chess/>`). |
| First-boot locale defaults (`src/CrossPointSettings.h`) | A fresh CN build starts with `Language::ZH_CN` and UTC+8 (`clockUtcOffsetQ = 80`); non-CN builds default to `Language::EN` and UTC+0. Saved UTC offsets are preserved across reflashes. When switching CN → global, the existing SKU/renderability guard resets Chinese to English, including legacy settings without a `langSku` marker. |

**Flash budget** (default `partitions.csv`, dual A/B app slot = 6.25 MB):

| Section | Bytes |
|---|---|
| Code + non-font data | ~3.0 MB |
| 3 CJK font headers 8/10/12pt (3500 SC + ASCII/Latin/CJK-punct) | ~1.10 MB |
| 1 CJK font header 14pt (7000 通用汉字 + extended symbols) | ~1.43 MB |
| 2 CJK font headers 16/18pt (i18n-only ~430 chars + ASCII/Latin/CJK-punct) | ~375 KB |
| i18n strings (EN + ZH_CN only) | ~16 KB |
| **Total** | **~6.05 MB / 6.25 MB (~92.3%)**, ~500 KB headroom |

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

The CN build keeps two size strategies stacked together to land in that
budget:

1. **`-flto=auto`** in `[env:gh_release_cn]`. Saves ~140 KB on `.text` via
   cross-TU dead-code elimination. The framework's default `-fno-lto`
   linker flag is stripped via `build_unflags` so the linker plugin
   picks up LTO IR in project objects; pre-built framework `.a` libs
   stay non-LTO and the GCC linker plugin handles the mix. `-Oz` was
   tested and produced byte-identical output to the framework's default
   `-Os` on this codebase, so it is *not* enabled (would only add
   configuration complexity).
2. **Per-size CJK character coverage** in `build-cn-builtin-fonts.sh`
   (see "Regenerating the CJK fonts" below). 8/10/12pt carry the small
   ~3500-char UI subset; **14pt carries the reader-default 7000 通用汉字
   subset plus extended symbol ranges**; 16/18pt carry an i18n-only subset
   (~430 chars from `chinese.yaml`). The tiering keeps the heavy 7000-char
   bitmap to a single point size and shrinks 16/18pt to i18n-only,
   saving ~1 MB vs running the full 7000-char subset at every size.

## Regenerating the CJK fonts

```bash
# 1. (One-time) install build deps into a venv
python3 -m venv /tmp/cn_font_venv
/tmp/cn_font_venv/bin/pip install -r lib/EpdFont/scripts/requirements.txt

# 2. Place NotoSansSC-Regular.otf into the source dir (gitignored).
#    Source: https://fonts.google.com/noto/specimen/Noto+Sans+SC
cp /path/to/NotoSansSC-Regular.otf lib/EpdFont/builtinFonts/source/NotoSansSC/

# 3. (Optional) regenerate the character lists. build-cn-builtin-fonts.sh
#    calls this for you in Step 0 unless SKIP_CHARSET=1 is set.
#    Writes BOTH cn_common_chars.txt (top-N + require-from) and
#    cn_i18n_chars.txt (require-from only — drives the 16/18pt subset).
PYTHON=/tmp/cn_font_venv/bin/python3 \
  python3 lib/EpdFont/scripts/build_cn_charset.py \
    --require-from lib/I18n/translations/chinese.yaml

# 4. Generate the 6 per-size CJK headers
PYTHON=/tmp/cn_font_venv/bin/python3 \
  bash lib/EpdFont/scripts/build-cn-builtin-fonts.sh

# 5. Build
pio run -e gh_release_cn
```

`build_cn_charset.py` prints the highest-frequency casualties (chars just
above and below the cutoff) so you can verify the trim looks reasonable.

## Force-including feature-specific glyphs

Each feature that needs CJK glyphs absent from both the 3500 SC pool *and*
the natural `chinese.yaml` STR_ values ships a small dedicated text file and
adds it to the `REQUIRE_FROM` array in
[build-cn-builtin-fonts.sh](../../lib/EpdFont/scripts/build-cn-builtin-fonts.sh).
`build_cn_charset.py --require-from <file>` regex-scans the file for every
CJK Unified Ideograph (`[一-鿿]`) and force-includes them in **both**
`cn_common_chars.txt` (8/10/12pt) and `cn_i18n_chars.txt` (16/18pt) —
so glyphs added via this mechanism render at every CN font size. The
14pt tier draws from the static `chars_7000_common.txt` pool instead and
empirically covers every char force-included by today's `REQUIRE_FROM`
list, so the require-from mechanism does **not** feed the 14pt header.
If you add a `cn_<feature>_chars.txt` containing a glyph outside the 7000
pool, audit it against `chars_7000_common.txt` and extend that pool too
(otherwise the feature will render at 8/10/12/16/18pt but show □ at 14pt).

Current example: [cn_almanac_chars.txt](../../lib/EpdFont/scripts/cn_almanac_chars.txt)
holds the chars `ChineseAlmanac.cpp` / `ChineseCalendarFace.cpp` need
beyond the standard pool:

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

## Expanding character coverage (pool, not --top)

`--top` is capped by the source pool size (`chars_3500_common.txt` has 3500
chars by construction — it is the vendored 教育部《现代汉语常用字表》).
Passing `--top 5000` will fail with `--top is >= pool size`.

To enlarge the renderable character set:

1. **For a handful of specific chars**: add a feature-scoped
   `cn_<feature>_chars.txt` as described above. One-line file + one-line
   append to `REQUIRE_FROM`. No pool expansion needed.
2. **For broad coverage gain** (e.g. classical literature, GB2312 Lv2): drop a
   larger source list into `lib/EpdFont/scripts/` (e.g. a `gb2312_full.txt`
   union of Lv1+Lv2) and point `SOURCE_FILE` in `build_cn_charset.py` at it.
   This is a deliberate Flash-budget decision — every 1000 extra chars adds
   roughly **200 KB** to the 8/10/12/14pt headers combined.

The hard ceiling is the 6.25 MB A/B-OTA slot. Today's headroom is ~1.4 MB,
so a ~7000-char SC+TC pool would still fit, but the upcoming OTA delta
shrinks proportionally.

## Known limitations

- **No bold/italic CJK glyphs**: the bitmaps come from a single NotoSansSC-Regular subset. UI elements that pass `EpdFontFamily::Style::Bold` render the regular weight under CN.
- **Font-size dropdown affects rendered size**: each reader size (12/14/16/18pt) and UI size (10/12pt) and small font (8pt) has its own bitmap header. Switching size really does swap glyph bitmaps.
- **Rare characters render as □ in reader at SMALL**: the SMALL (12pt) bitmap uses the 3500-char pool, which covers all of modern SC but omits classical / scientific rarities, Traditional Chinese variants, and most niche surnames/place names. MEDIUM (14pt, reader default) now uses the 7000 通用汉字 pool and covers most modern names, place names, and regulated chars from the 2013 通用规范汉字表. Expand SMALL by adding a feature-scoped `cn_<feature>_chars.txt` or by enlarging the pool — see "Expanding character coverage" above. Bumping `--top` alone does nothing.
- **CJK in reader at LARGE/EXTRA_LARGE shows blank** for chars outside the i18n subset — by design, since 16/18pt reader sizes are tuned for English EPUB. Switch to MEDIUM to read Chinese.
- **`FontDecompressor` is bypassed for CJK** by design — bitmaps are stored raw because compressing 6 fonts × ~50 KB groups fragments the heap on boot.
- **No Traditional Chinese support**: the build is explicitly SC-only (`_language_code: ZH_CN`, no `zh-TW`/`zh-HK` yaml). TC glyphs are not in any pool; TC strings would render as missing-glyph placeholders.

## Files

| Path | Role |
|---|---|
| `lib/EpdFont/scripts/build_cn_charset.py` | Rank a pool by wordfreq Zipf, emit top-N + i18n force-includes. Pool capped at `chars_3500_common.txt` size (3500). |
| `lib/EpdFont/scripts/chars_3500_common.txt` | Source pool — 现代汉语常用字表, 3500 chars (committed). To expand coverage, swap this file (see "Expanding character coverage"). |
| `lib/EpdFont/scripts/cn_common_chars.txt` | Generated full subset, drives 8/10/12pt (committed). Single-line UTF-8, sorted by codepoint. |
| `lib/EpdFont/scripts/chars_7000_common.txt` | Static 7000 通用汉字 pool, drives 14pt (committed). One-line UTF-8, codepoint-sorted. Extracted from the 2013 国家通用规范汉字表-aligned vendor list; fully contains today's `cn_common_chars.txt`, `cn_i18n_chars.txt`, and `cn_almanac_chars.txt`. |
| `lib/EpdFont/scripts/cn_i18n_chars.txt` | Generated i18n-only subset, drives 16/18pt (committed). Contains every CJK char found in `--require-from` inputs. |
| `lib/EpdFont/scripts/build-cn-builtin-fonts.sh` | pyftsubset → fontconvert.py pipeline, six headers. Default re-runs `build_cn_charset.py`; set `SKIP_CHARSET=1` to reuse the current `cn_common_chars.txt`. The `REQUIRE_FROM=(...)` array at the top lists every file scanned for force-included CJK chars — add new feature-scoped `cn_*_chars.txt` files here. |
| `lib/EpdFont/scripts/cn_almanac_chars.txt` | Feature-scoped force-include for `ChineseAlmanac.cpp` / `ChineseCalendarFace.cpp` — ganzhi stems/branches + lunar-row vocab that aren't in the pool or any `chinese.yaml` STR_ value. Single-line UTF-8. |
| `lib/EpdFont/builtinFonts/notosans_cjk_{8,10,12,14,16,18}.h` | Generated bitmap headers (committed). Should always match `cn_common_chars.txt` (8/10/12pt), `chars_7000_common.txt` (14pt), and `cn_i18n_chars.txt` (16/18pt) — see consistency check below. |
| `lib/EpdFont/builtinFonts/source/NotoSansSC/` | TTF source dir (gitignored except for `.gitignore`). Drop `NotoSansSC-Regular.otf` here. |
| `lib/I18n/translations/chinese.yaml` | Simplified Chinese translations (`_language_code: ZH_CN`); also fed to `--require-from` so every CJK char in `STR_*: "value"` lines is forced into both subsets. Do **not** hide font-only chars in `#` comments — use a `cn_<feature>_chars.txt` file instead. |

## Consistency check

After regenerating, confirm the committed bitmap headers match the committed
charset files:
- 8/10/12pt headers should equal `len(cn_common_chars.txt unique CJK) + ~497`
  (ASCII/Latin-1/CJK-punct from `--unicodes`)
- 14pt header should equal `len(chars_7000_common.txt) + ~700`
  (extra ~200 comes from the LARGE tier's added symbol ranges)
- 16/18pt headers should equal `len(cn_i18n_chars.txt) + ~497`

```bash
python3 -c "
import re
for sz, subset in [(8, 'cn_common_chars'),
                   (14, 'chars_7000_common'),
                   (16, 'cn_i18n_chars')]:
    chars = open(f'lib/EpdFont/scripts/{subset}.txt').read()
    n_cjk = len(set(c for c in chars if '一' <= c <= '鿿'))
    hdr = open(f'lib/EpdFont/builtinFonts/notosans_cjk_{sz}.h').read()
    n_glyphs = sum(1 for ln in hdr.splitlines() if re.match(r'\s*\{\s*\d+,', ln))
    print(f'{sz}pt: charset CJK={n_cjk}, header glyphs={n_glyphs}, latin/punct ≈ {n_glyphs - n_cjk}')
"
```

If `header glyphs` is significantly larger than the expected delta, the
header is stale (most likely committed from an older/larger charset) — re-run
the regeneration steps.
