#pragma once

#include "AirPageImageStore.h"

namespace airpage {

class AirPageWallpaper final {
 public:
  static bool install(const SelectedImage& selected);

 private:
  static bool writePart(const SelectedImage& selected);
  static bool copyFile(const char* sourcePath, const char* targetPath);
};

}  // namespace airpage
