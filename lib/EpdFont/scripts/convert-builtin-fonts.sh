#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(12 14 16 18)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path --2bit --compress --pnum > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path $hebrew_path $viet_path \
      --additional-intervals 0x05D0,0x05EA > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA > ../builtinFonts/notosans_8_regular.h

# Chinese chess piece characters: 仕 俥 馬 兵 卒 士 将 帅 炮 相 砲 象 车 马
# Source font is provided as a pre-subset TTF containing only those 14 glyphs
# plus '|' (U+007C), which fontconvert.py uses as a descender heuristic.
if [ -f ../builtinFonts/source/ChineseChess/ChineseChess.ttf ]; then
  python fontconvert.py chinese_chess_16 16 ../builtinFonts/source/ChineseChess/ChineseChess.ttf --2bit --compress \
    --additional-intervals 0x4ED5,0x4ED5 \
    --additional-intervals 0x4FE5,0x4FE5 \
    --additional-intervals 0x99AC,0x99AC \
    --additional-intervals 0x5175,0x5175 \
    --additional-intervals 0x5352,0x5352 \
    --additional-intervals 0x58EB,0x58EB \
    --additional-intervals 0x5C06,0x5C06 \
    --additional-intervals 0x5E05,0x5E05 \
    --additional-intervals 0x70AE,0x70AE \
    --additional-intervals 0x76F8,0x76F8 \
    --additional-intervals 0x7832,0x7832 \
    --additional-intervals 0x8C61,0x8C61 \
    --additional-intervals 0x8F66,0x8F66 \
    --additional-intervals 0x9A6C,0x9A6C \
    > ../builtinFonts/chinese_chess_16.h
  echo "Generated ../builtinFonts/chinese_chess_16.h"
else
  echo "Skipping chinese_chess_16: ChineseChess.ttf source not present"
fi

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
