#pragma once

#include "AirPageImageStore.h"

class GfxRenderer;
struct Rect;

namespace airpage {

class AirPageImageRenderer final {
 public:
  static void resetSessionFailures();
  static void releaseSessionResources();
  enum class Result : uint8_t { Success, OutOfMemory, Failed };
  static Result render(GfxRenderer& renderer, const Rect& viewport, const SelectedImage& selected);

 private:
  static Rect fittedBounds(const Rect& viewport, const ImageInfo& image);
};

}  // namespace airpage
