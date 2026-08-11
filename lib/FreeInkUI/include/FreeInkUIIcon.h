#pragma once

// FreeInk SDK — freeink::Icon → FreeInkUI BitmapRef bridge (opt-in).
//
// The Icons library (libs/assets/Icons) generates crisp 1-bpp icons at any
// size from Lucide SVGs via tools/gen_icons.py:
//
//   gen_icons.py --manifest icons.txt --svgdir libs/assets/Icons/lucide/icons \
//                --sizes 16,22,24,48 --out src/icons_gen.h
//
// This header adapts those structs to the BitmapRef every FreeInkUI component
// takes (Mask1 = the Icon convention, bit 0 = draw). Opt-in like
// FreeInkUIGfxRenderer.h: only compilable in firmwares that also add the
// Icons library to lib_deps.

#include <Icon.h>

#include "FreeInkUI.h"

namespace freeink {
namespace ui {

inline BitmapRef bitmapFromIcon(const Icon& icon) {
  BitmapRef ref;
  ref.data = icon.bits;
  ref.width = icon.w;
  ref.height = icon.h;
  ref.format = BitmapFormat::Mask1;
  return ref;
}

}  // namespace ui
}  // namespace freeink
