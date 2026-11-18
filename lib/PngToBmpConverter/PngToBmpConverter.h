#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
 public:
  enum class Output { Mono1, Gray2, Gray8 };

 private:
  static bool pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         Output output, bool crop = true, bool preserveTransparency = false,
                                         bool originalThresholds = false);
  static bool pngFileToBmpFileInternal(const char* pngPath, const char* bmpPath, bool crop, bool preserveTransparency,
                                       Output output = Output::Gray2);

 public:
  static bool pngFileToBmpFile(const char* pngPath, const char* bmpPath, bool crop = true,
                               Output output = Output::Gray2);
  static bool pngFileToTransparentBmpFile(const char* pngPath, const char* bmpPath, bool crop = true);
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true, bool originalThresholds = false,
                                 Output output = Output::Gray2);
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                         Output output = Output::Gray2);
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
};
