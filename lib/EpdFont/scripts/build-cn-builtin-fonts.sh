#!/bin/bash
#
# Generates the per-size CJK font headers used by the ENABLE_CHINESE_VERSION
# build. Coverage tiers per point size:
#
#   8/10/12pt (COMMON, UI + reader SMALL) : 3500 常用字 ∪ i18n
#                                            (cn_common_chars.txt)
#   14/16/18pt (I18N, reader MEDIUM/LARGE/EXTRA_LARGE) : UI-only chars
#                                                          (cn_i18n_chars.txt)
#
# Every tier also ships ASCII + Latin-1 + CJK punctuation + full-width forms.
#
# Pipeline:
#   1. pyftsubset trims NotoSansSC-Regular.otf to the requested character set
#   2. fontconvert.py emits a 2-bit raw bitmap header for each point size
#
# Raw bitmaps (no --compress): with 6 simultaneously-loaded CJK fonts each
# group would need ~50 KB scratch, fragmenting the heap on boot and crashing
# FontDecompressor with std::bad_alloc. Latin fonts ship compressed because
# their groups are tiny (~5 KB).
#
# Set PYTHON env var to override the interpreter (e.g. a virtualenv with
# freetype-py and fonttools installed):
#   PYTHON=/path/to/venv/bin/python bash build-cn-builtin-fonts.sh

set -euo pipefail

cd "$(dirname "$0")"

# Project requires fontTools + freetype-py — both Python 3 only. Default to
# python3 (override with PYTHON=/path/to/venv/bin/python if a venv is needed).
PYTHON="${PYTHON:-python3}"

SOURCE_OTF="../builtinFonts/source/NotoSansSC/NotoSansSC-Regular.otf"
# Frequency-ranked subset produced by build_cn_charset.py. Defaults to the
# top 3500 most common Chinese characters by wordfreq Zipf score, plus every
# CJK ideograph used in the Chinese i18n strings (force-included so UI text
# can never silently drop glyphs — see build_cn_charset.py docstring).
# Regenerate manually with: python3 build_cn_charset.py --top <N> --require-from ...
CHARSET_FILE="cn_common_chars.txt"
# Force-include sources fed to build_cn_charset.py --require-from. Each
# feature that needs CJK glyphs not already covered at the point size where
# it renders adds its own cn_<feature>_chars.txt here.
# cn_almanac_chars.txt: ganzhi + lunar-row chars for ChineseCalendarFace.
REQUIRE_FROM=(../../I18n/translations/chinese.yaml cn_almanac_chars.txt)
TMP_DIR="instanced_fonts/NotoSansSC"
SUBSET_OTF="$TMP_DIR/NotoSansSC-Regular.cncommon.otf"
# Tiny OTF holding only the CJK chars that appear in i18n/feature sources (747
# chars). Used to build the 14pt/16pt/18pt bitmap headers — those reader sizes
# rely on an SD-card font for broad Chinese EPUB coverage, but UI strings (game
# win banners etc.) still need to render at every size.
I18N_OTF="$TMP_DIR/NotoSansSC-Regular.i18nonly.otf"
I18N_CHARSET_FILE="cn_i18n_chars.txt"

# Font sizes split by character coverage:
#   COMMON → cn_common_chars.txt subset (3500 common chars + required)
#   I18N  → i18n-only subset (reader MEDIUM/LARGE/EXTRA_LARGE + UI text)
CN_FONT_SIZES_SMALL=(8 10 12)
CN_FONT_SIZES_I18N=(14 16 18)

if [ ! -f "$SOURCE_OTF" ]; then
  echo "Error: $SOURCE_OTF not found." >&2
  echo "Drop NotoSansSC-Regular.otf into lib/EpdFont/builtinFonts/source/NotoSansSC/." >&2
  exit 1
fi

# Step 0: refresh cn_common_chars.txt so any CJK chars used by Chinese UI
# strings are force-included (GB2312 Lv1 omits common modern chars like 浏).
# Set SKIP_CHARSET=1 to keep an existing cn_common_chars.txt as-is (useful when
# you've manually run build_cn_charset.py with custom --top).
if [ -z "${SKIP_CHARSET:-}" ]; then
  require_args=()
  for f in "${REQUIRE_FROM[@]}"; do
    require_args+=(--require-from "$f")
  done
  echo "Refreshing $CHARSET_FILE (require-from: ${REQUIRE_FROM[*]})..."
  "$PYTHON" build_cn_charset.py "${require_args[@]}"
fi

if [ ! -f "$CHARSET_FILE" ]; then
  echo "Error: $CHARSET_FILE not found in $(pwd)." >&2
  exit 1
fi

mkdir -p "$TMP_DIR"

# Note on charset files:
#   - $CHARSET_FILE (cn_common_chars.txt): full subset for 8/10/12pt.
#     Produced by build_cn_charset.py (Step 0 above) — pool ∪ required.
#   - $I18N_CHARSET_FILE (cn_i18n_chars.txt): require-from only, for the
#     tiny 14pt/16pt/18pt subset. Also produced by build_cn_charset.py as a side
#     output whenever --require-from is passed.
if [ ! -f "$I18N_CHARSET_FILE" ]; then
  echo "Error: $I18N_CHARSET_FILE not found in $(pwd)." >&2
  echo "       Run build_cn_charset.py with at least one --require-from arg," >&2
  echo "       or remove SKIP_CHARSET=1 so Step 0 above runs it for you." >&2
  exit 1
fi

# Step 1a: subset the OTF down to cn_common_chars (3500 ∪ required) + ASCII +
# Latin-1 + CJK punctuation. Used by the 8/10/12pt bitmap headers.
echo "Subsetting $(basename "$SOURCE_OTF") → $(basename "$SUBSET_OTF") (common)..."
"$PYTHON" -m fontTools.subset "$SOURCE_OTF" \
  --output-file="$SUBSET_OTF" \
  --text-file="$CHARSET_FILE" \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2026,U+3000-303F,U+FF00-FF9F,U+FFA1-FFEF,U+FFFD" \
  --layout-features='*' \
  --notdef-outline \
  --recommended-glyphs \
  --no-hinting \
  --drop-tables+=DSIG,GSUB,GPOS

# Step 1b: subset down to i18n-only CJK + ASCII + Latin-1 + CJK punctuation.
# Used by the 14pt/16pt/18pt bitmap headers.
echo "Subsetting $(basename "$SOURCE_OTF") → $(basename "$I18N_OTF") (i18n)..."
"$PYTHON" -m fontTools.subset "$SOURCE_OTF" \
  --output-file="$I18N_OTF" \
  --text-file="$I18N_CHARSET_FILE" \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2026,U+3000-303F,U+FF00-FF9F,U+FFA1-FFEF,U+FFFD" \
  --layout-features='*' \
  --notdef-outline \
  --recommended-glyphs \
  --no-hinting \
  --drop-tables+=DSIG,GSUB,GPOS

# Step 2: emit one 2-bit raw bitmap header per requested point size.
# (See file header for the rationale behind skipping --compress.)
# fontconvert.py auto-skips code points missing from the source OTF, so passing
# the broad CJK interval is fine — the i18n OTF will only emit 747 CJK glyphs.
emit_size() {
  local size="$1"
  local otf="$2"
  local font_name="notosans_cjk_${size}"
  local output_path="../builtinFonts/${font_name}.h"
  # Write to a temp file first, then atomically mv on success. Otherwise a
  # crash in fontconvert.py leaves the target as a zero-byte file (the shell
  # truncates the redirect target *before* the python process runs), which
  # the loop below would then happily report as "0 bytes" and commit.
  local tmp_path="${output_path}.tmp"
  echo "Generating ${output_path} from $(basename "$otf")..."
  "$PYTHON" fontconvert.py "$font_name" "$size" "$otf" \
    --2bit \
    --additional-intervals 0x4E00,0x9FFF \
    --additional-intervals 0x3000,0x303F \
    --additional-intervals 0xFF00,0xFFEF \
    > "$tmp_path"
  if [ ! -s "$tmp_path" ]; then
    echo "Error: fontconvert.py produced empty $tmp_path for ${font_name}" >&2
    rm -f "$tmp_path"
    exit 1
  fi
  mv "$tmp_path" "$output_path"
  echo "  $(wc -c < "$output_path") bytes ($(grep -E "Bitmaps\[" "$output_path" | head -1))"
}

for size in "${CN_FONT_SIZES_SMALL[@]}"; do
  emit_size "$size" "$SUBSET_OTF"
done
for size in "${CN_FONT_SIZES_I18N[@]}"; do
  emit_size "$size" "$I18N_OTF"
done

echo ""
echo "Done. Generated $((${#CN_FONT_SIZES_SMALL[@]} + ${#CN_FONT_SIZES_I18N[@]})) CJK font headers in ../builtinFonts/"
