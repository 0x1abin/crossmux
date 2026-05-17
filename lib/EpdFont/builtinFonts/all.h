#pragma once

#ifdef ENABLE_CHINESE_VERSION
// Chinese build: one CJK font header per UI/reader point size. All sizes are
// raw 2-bit bitmaps (fontconvert.py's --compress is NOT used because the
// large per-group buffers fragment the heap on a 5+ font load — see
// build-cn-builtin-fonts.sh).
//
// Character coverage is non-uniform across sizes:
//   - 8/10/12/14pt: full frequency-ranked subset (top-N of
//     现代汉语常用字表 + i18n require-from chars) — sized for reader
//     SMALL/MEDIUM and UI at all readable sizes.
//   - 16/18pt: i18n-only subset (just the CJK chars used by UI strings,
//     plus ASCII + Latin-1 + CJK punctuation) — sized for reader
//     LARGE/EXTRA_LARGE (intended for English EPUB). Chinese EPUB text
//     at these sizes shows blank for any char outside the i18n subset;
//     UI strings still render because their chars are always included.
#include <builtinFonts/chinese_chess_16.h>
#include <builtinFonts/notosans_cjk_8.h>
#include <builtinFonts/notosans_cjk_10.h>
#include <builtinFonts/notosans_cjk_12.h>
#include <builtinFonts/notosans_cjk_14.h>
#include <builtinFonts/notosans_cjk_16.h>
#include <builtinFonts/notosans_cjk_18.h>
#else
#include <builtinFonts/notoserif_12_bold.h>
#include <builtinFonts/notoserif_12_bolditalic.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_12_regular.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_bolditalic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_bold.h>
#include <builtinFonts/notoserif_16_bolditalic.h>
#include <builtinFonts/notoserif_16_italic.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_bold.h>
#include <builtinFonts/notoserif_18_bolditalic.h>
#include <builtinFonts/notoserif_18_italic.h>
#include <builtinFonts/notoserif_18_regular.h>
#include <builtinFonts/notosans_8_regular.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_bolditalic.h>
#include <builtinFonts/notosans_14_italic.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_bolditalic.h>
#include <builtinFonts/notosans_16_italic.h>
#include <builtinFonts/notosans_16_regular.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_bolditalic.h>
#include <builtinFonts/notosans_18_italic.h>
#include <builtinFonts/notosans_18_regular.h>
#include <builtinFonts/opendyslexic_10_bold.h>
#include <builtinFonts/opendyslexic_10_bolditalic.h>
#include <builtinFonts/opendyslexic_10_italic.h>
#include <builtinFonts/opendyslexic_10_regular.h>
#include <builtinFonts/opendyslexic_12_bold.h>
#include <builtinFonts/opendyslexic_12_bolditalic.h>
#include <builtinFonts/opendyslexic_12_italic.h>
#include <builtinFonts/opendyslexic_12_regular.h>
#include <builtinFonts/opendyslexic_14_bold.h>
#include <builtinFonts/opendyslexic_14_bolditalic.h>
#include <builtinFonts/opendyslexic_14_italic.h>
#include <builtinFonts/opendyslexic_14_regular.h>
#include <builtinFonts/opendyslexic_8_bold.h>
#include <builtinFonts/opendyslexic_8_bolditalic.h>
#include <builtinFonts/opendyslexic_8_italic.h>
#include <builtinFonts/opendyslexic_8_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>
#endif
