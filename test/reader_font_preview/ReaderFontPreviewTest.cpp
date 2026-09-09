#include <cassert>

#include "src/activities/reader/ReaderFontPreview.h"

int main() {
  using Decision = ReaderFontPreview::Decision;
  ReaderFontPreview preview;
  static_assert(sizeof(preview) <= 36);
  assert(preview.finish("Serif", 16, false) == Decision::Keep);

  // Changing family and changing size both require consent without a cache.
  preview.begin("Serif", 16, true);
  assert(preview.finish("Sans", 16, false) == Decision::Ask);
  preview.begin("Serif", 16, true);
  assert(preview.finish("Serif", 18, false) == Decision::Ask);

  // Nested pickers / panel revisits must not replace the original selection.
  preview.begin("Serif", 16, true);
  preview.begin("Sans", 18, false);
  preview.begin("Sans", 20, false);
  assert(preview.finish("Sans", 20, false) == Decision::Ask);
  assert(!preview.active());

  // Restoring the original selection also restores its acceleration preference.
  preview.begin("Serif", 16, true);
  preview.begin("Sans", 18, false);
  assert(preview.finish("Serif", 16, true) == Decision::Enable);
  preview.begin("Serif", 16, false);
  assert(preview.finish("Serif", 16, true) == Decision::Disable);

  preview.begin("Serif", 16, false);
  assert(preview.finish("Sans", 18, true) == Decision::Enable);
  preview.begin("Serif", 16, true);
  assert(preview.finish("", 16, false) == Decision::Disable);
  preview.begin("", 16, false);
  assert(preview.finish("Sans", 16, false) == Decision::Ask);

  // Declining ends the visit: merely reopening (or changing layout) cannot retry.
  assert(preview.finish("Sans", 16, false) == Decision::Keep);
  preview.begin("Sans", 16, false);
  assert(preview.finish("Sans", 16, false) == Decision::Disable);
  preview.begin("Sans", 16, false);
  assert(preview.finish("Sans", 18, false) == Decision::Ask);
}
