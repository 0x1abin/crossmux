#include "AirPageWallpaper.h"

#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"
#include "JpegToBmpConverter.h"
#include "util/SleepCover.h"

namespace airpage {

namespace {

constexpr char kSleepImagePartPath[] = "/sleep.bmp.part";
constexpr size_t kCopyBufferSize = 128;

template <typename Display>
JpegToBmpConverter::Output wallpaperOutput(const Display& panel) {
  if constexpr (requires { panel.getGrayscaleLevels(); }) {
    if (panel.getGrayscaleLevels() == 16) return JpegToBmpConverter::Output::Gray8;
  }
  return JpegToBmpConverter::Output::Gray2;  // Legacy simulator HAL has four levels.
}

}  // namespace

bool AirPageWallpaper::copyFile(const char* sourcePath, const char* targetPath) {
  Storage.remove(targetPath);
  HalFile input;
  HalFile output;
  if (!Storage.openFileForRead("AIRP", sourcePath, input) || !Storage.openFileForWrite("AIRP", targetPath, output)) {
    return false;
  }

  const uint64_t expected = input.fileSize64();
  uint64_t copied = 0;
  uint8_t buffer[kCopyBufferSize];
  while (input.available() > 0) {
    const size_t want = std::min<size_t>(sizeof(buffer), static_cast<size_t>(input.available()));
    const int bytesRead = input.read(buffer, want);
    if (bytesRead <= 0 || output.write(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
      return false;
    }
    copied += static_cast<uint64_t>(bytesRead);
  }
  output.flush();
  return copied == expected;
}

bool AirPageWallpaper::writePart(const SelectedImage& selected) {
  Storage.remove(kSleepImagePartPath);
  switch (selected.image.format) {
    case ImageFormat::None:
      return false;
    case ImageFormat::Bmp:
      return copyFile(selected.path, kSleepImagePartPath);
    case ImageFormat::Jpeg: {
      HalFile input;
      HalFile output;
      if (!Storage.openFileForRead("AIRP", selected.path, input) ||
          !Storage.openFileForWrite("AIRP", kSleepImagePartPath, output)) {
        return false;
      }
      const auto format = wallpaperOutput(display);
      const bool converted = JpegToBmpConverter::jpegFileToBmpStream(input, output, /*crop=*/false, format);
      output.flush();
      return converted;
    }
  }
  return false;
}

bool AirPageWallpaper::install(const SelectedImage& selected) {
  if (!sleepCover::recover()) return false;
  if (!writePart(selected)) {
    Storage.remove(kSleepImagePartPath);
    return false;
  }

  const bool installed = sleepCover::commit(CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM);
  if (!installed) Storage.remove(kSleepImagePartPath);
  return installed;
}

}  // namespace airpage
