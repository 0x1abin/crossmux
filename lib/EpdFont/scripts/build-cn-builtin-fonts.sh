#!/bin/bash
#
# Generates the per-size CJK font headers used by the ENABLE_CHINESE_VERSION
# build. Each header covers ASCII + the top-N frequency-ranked Chinese chars
# (default N=3000, see build_cn_charset.py / cn_common_chars.txt) + full-width
# CJK punctuation.
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

set -e

cd "$(dirname "$0")"

PYTHON="${PYTHON:-python}"

SOURCE_OTF="../builtinFonts/source/NotoSansSC/NotoSansSC-Regular.otf"
# Frequency-ranked subset produced by build_cn_charset.py. Defaults to the
# top 3000 most common Chinese characters by wordfreq Zipf score, plus every
# CJK ideograph used in the Chinese i18n strings (force-included so UI text
# can never silently drop glyphs — see build_cn_charset.py docstring).
# Regenerate manually with: python3 build_cn_charset.py --top <N> --require-from ...
CHARSET_FILE="cn_common_chars.txt"
# Force-include sources fed to build_cn_charset.py --require-from. Add more
# i18n yamls here if additional CN-targeted locales are introduced.
REQUIRE_FROM=(../../I18n/translations/chinese.yaml)
TMP_DIR="instanced_fonts/NotoSansSC"
SUBSET_OTF="$TMP_DIR/NotoSansSC-Regular.cncommon.otf"

CN_FONT_SIZES=(8 10 12 14 16 18)

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

# Step 1: subset the OTF down to GB2312-Lv1 hanzi + ASCII + CJK punctuation +
# common typographic marks. pyftsubset ships with fontTools (see requirements.txt).
echo "Subsetting $(basename "$SOURCE_OTF") → $(basename "$SUBSET_OTF")..."
"$PYTHON" -m fontTools.subset "$SOURCE_OTF" \
  --output-file="$SUBSET_OTF" \
  --text-file="$CHARSET_FILE" \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2026,U+3000-303F,U+FF00-FFEF,U+FFFD" \
  --layout-features='*' \
  --notdef-outline \
  --recommended-glyphs \
  --no-hinting \
  --drop-tables+=DSIG,GSUB,GPOS

# Step 2: emit one 2-bit raw bitmap header per requested point size.
# (See file header for the rationale behind skipping --compress.)
for size in "${CN_FONT_SIZES[@]}"; do
  font_name="notosans_cjk_${size}"
  output_path="../builtinFonts/${font_name}.h"
  echo "Generating ${output_path}..."
  "$PYTHON" fontconvert.py "$font_name" "$size" "$SUBSET_OTF" \
    --2bit \
    --additional-intervals 0x4E00,0x9FFF \
    --additional-intervals 0x3000,0x303F \
    --additional-intervals 0xFF00,0xFFEF \
    > "$output_path"
  echo "  $(wc -c < "$output_path") bytes ($(grep -E "Bitmaps\[" "$output_path" | head -1))"
done

echo ""
echo "Done. Generated ${#CN_FONT_SIZES[@]} CJK font headers in ../builtinFonts/"
