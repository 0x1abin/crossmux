#pragma once

#include <HalStorage.h>

class Print;
class ZipFile;

class JpegToBmpConverter {
 public:
  enum class Output { Mono1, Gray2, Gray8 };

 private:
  static bool jpegFileToBmpStreamInternal(HalFile& jpegFile, Print& bmpOut, int targetWidth, int targetHeight,
                                          Output output, bool crop = true, bool originalThresholds = false);

 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop = true, Output output = Output::Gray2,
                                  bool originalThresholds = false);
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop, bool originalThresholds);
  // Convert with custom target size (for thumbnails and image previews).
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                          bool crop = true);
  // Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                              int targetMaxHeight);
};
