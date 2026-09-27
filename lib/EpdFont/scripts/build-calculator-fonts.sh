#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

PYTHON="${PYTHON:-python3}"
# Includes scientific notation and the missing-glyph replacement; errors use UI fonts.
characters='0123456789 +-×÷.%=e�'
for style in Regular Bold; do
  name="calculator_18_$(echo "$style" | tr '[:upper:]' '[:lower:]')"
  output="../builtinFonts/${name}.h"
  "$PYTHON" fontconvert.py "$name" 18 "../builtinFonts/source/NotoSans/NotoSans-${style}.ttf" \
    --2bit --compress --pnum --zopfli --characters "$characters" > "${output}.tmp"
  mv "${output}.tmp" "$output"
done
