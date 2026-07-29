#include "AirPageWallpaper.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>

#include "CrossPointSettings.h"
#include "JpegToBmpConverter.h"

namespace airpage {

namespace {

constexpr char kSleepImagePath[] = "/sleep.bmp";
constexpr char kSleepImagePartPath[] = "/sleep.bmp.part";
constexpr char kSleepImageBackupPath[] = "/sleep.bmp.bak";
constexpr size_t kCopyBufferSize = 128;

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
      const bool converted = JpegToBmpConverter::jpegFileToBmpStream(input, output, /*crop=*/false);
      output.flush();
      return converted;
    }
  }
  return false;
}

bool AirPageWallpaper::install(const SelectedImage& selected) {
  recoverInterruptedTransaction();
  if (!writePart(selected)) {
    Storage.remove(kSleepImagePartPath);
    return false;
  }

  ImageInfo generated;
  if (!AirPageImageStore::inspectImage(kSleepImagePartPath, generated) || generated.format != ImageFormat::Bmp) {
    Storage.remove(kSleepImagePartPath);
    return false;
  }

  Storage.remove(kSleepImageBackupPath);
  const bool hadPrevious = Storage.exists(kSleepImagePath);
  if (hadPrevious && !Storage.rename(kSleepImagePath, kSleepImageBackupPath)) {
    Storage.remove(kSleepImagePartPath);
    return false;
  }
  if (!Storage.rename(kSleepImagePartPath, kSleepImagePath)) {
    if (hadPrevious) Storage.rename(kSleepImageBackupPath, kSleepImagePath);
    return false;
  }

  const uint8_t previousMode = SETTINGS.sleepScreen;
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  if (!SETTINGS.saveToFile()) {
    SETTINGS.sleepScreen = previousMode;
    (void)SETTINGS.saveToFile();
    Storage.remove(kSleepImagePath);
    if (hadPrevious) Storage.rename(kSleepImageBackupPath, kSleepImagePath);
    return false;
  }

  Storage.remove(kSleepImageBackupPath);
  return true;
}

void AirPageWallpaper::recoverInterruptedTransaction() {
  Storage.remove(kSleepImagePartPath);
  if (!Storage.exists(kSleepImageBackupPath)) return;

  if (Storage.exists(kSleepImagePath) && SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    Storage.remove(kSleepImageBackupPath);
    return;
  }

  Storage.remove(kSleepImagePath);
  if (!Storage.rename(kSleepImageBackupPath, kSleepImagePath)) {
    LOG_ERR("AIRP", "Could not recover previous sleep screen");
  }
}

}  // namespace airpage
