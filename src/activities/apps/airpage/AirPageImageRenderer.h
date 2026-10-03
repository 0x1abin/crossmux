#pragma once

#include "AirPageImageStore.h"

class GfxRenderer;
struct Rect;

namespace airpage {

class AirPageImageRenderer final {
 public:
  static void resetSessionFailures();
  static void releaseSessionResources();
  static void cleanScreen(GfxRenderer& renderer);
  enum class Result : uint8_t { Success, OutOfMemory, Failed };
  static Result render(GfxRenderer& renderer, const Rect& viewport, const SelectedImage& selected,
                       bool cleanBeforeDisplay = false);

 private:
  static Rect fittedBounds(const Rect& viewport, const ImageInfo& image);
};

}  // namespace airpage
