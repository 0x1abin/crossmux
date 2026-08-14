#include "ReadingBackground.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PngToBmpConverter.h>

namespace readingBackground {
namespace {
constexpr const char* TEMP_PATH = "/.crosspoint/background/reading_bg.tmp";
constexpr const char* BACKUP_PATH = "/.crosspoint/background/reading_bg.bak";
constexpr const char* SOURCE_BITMAP_PATH = "/.crosspoint/background/source.bmp";

void removeIfPresent(const char* path) {
  if (Storage.exists(path)) Storage.remove(path);
}

bool recoverBackup() {
  if (Storage.exists(CACHE_PATH) || !Storage.exists(BACKUP_PATH)) return true;
  return Storage.rename(BACKUP_PATH, CACHE_PATH);
}
}  // namespace

bool createCache(GfxRenderer& renderer, const char* bitmapPath) {
  if (!bitmapPath || !Storage.ensureDirectoryExists(CACHE_DIR)) return false;
  if (!recoverBackup()) return false;
  removeIfPresent(TEMP_PATH);

  bool generated = false;
  const auto originalOrientation = renderer.getOrientation();
  const auto originalMode = renderer.getRenderMode();
  {
    HalFile bitmapFile;
    HalFile cacheFile;
    if (Storage.openFileForRead("ReadingBackground", bitmapPath, bitmapFile) &&
        Storage.openFileForWrite("ReadingBackground", TEMP_PATH, cacheFile)) {
      Bitmap bitmap(bitmapFile, false);
      CacheHeader header;
      header.displayWidth = renderer.getDisplayWidth();
      header.displayHeight = renderer.getDisplayHeight();
      header.frameSize = static_cast<uint32_t>(renderer.getBufferSize());
      generated =
          bitmap.parseHeaders() == BmpReaderError::Ok && cacheFile.write(&header, sizeof(header)) == sizeof(header);

      renderer.setRenderMode(GfxRenderer::BW);
      for (uint8_t orientation = 0; generated && orientation < ORIENTATION_COUNT; ++orientation) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
        renderer.clearScreen();
        generated =
            bitmap.rewindToData() == BmpReaderError::Ok &&
            renderer.drawBitmapCropToFill(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()) &&
            cacheFile.write(renderer.getFrameBuffer(), renderer.getBufferSize()) == renderer.getBufferSize();
      }
      cacheFile.flush();
    }
  }
  renderer.setOrientation(originalOrientation);
  renderer.setRenderMode(originalMode);

  if (!generated) {
    removeIfPresent(TEMP_PATH);
    return false;
  }

  const bool hadCache = Storage.exists(CACHE_PATH);
  removeIfPresent(BACKUP_PATH);
  if (hadCache && !Storage.rename(CACHE_PATH, BACKUP_PATH)) {
    removeIfPresent(TEMP_PATH);
    return false;
  }
  if (!Storage.rename(TEMP_PATH, CACHE_PATH)) {
    if (hadCache) Storage.rename(BACKUP_PATH, CACHE_PATH);
    removeIfPresent(TEMP_PATH);
    return false;
  }
  removeIfPresent(BACKUP_PATH);
  return true;
}

bool createCacheFromPng(GfxRenderer& renderer, const char* pngPath) {
  if (!pngPath || !Storage.ensureDirectoryExists(CACHE_DIR)) return false;

  bool converted;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    converted = PngToBmpConverter::pngFileToBmpFile(pngPath, SOURCE_BITMAP_PATH, true);
  }
  if (!converted) return false;

  const bool created = createCache(renderer, SOURCE_BITMAP_PATH);
  removeIfPresent(SOURCE_BITMAP_PATH);
  return created;
}

bool load(GfxRenderer& renderer) {
  const uint8_t orientation = static_cast<uint8_t>(renderer.getOrientation());
  if (!isValidOrientation(orientation)) return false;
  if (!recoverBackup()) return false;

  HalFile file;
  if (!Storage.openFileForRead("ReadingBackground", CACHE_PATH, file)) return false;
  CacheHeader header;
  if (file.read(&header, sizeof(header)) != sizeof(header) ||
      !isValidHeader(header, renderer.getDisplayWidth(), renderer.getDisplayHeight(), renderer.getBufferSize(),
                     file.fileSize64()) ||
      !file.seek64(frameOffset(orientation, header.frameSize)) ||
      file.read(renderer.getFrameBuffer(), header.frameSize) != static_cast<int>(header.frameSize)) {
    renderer.clearScreen();
    return false;
  }
  return true;
}

}  // namespace readingBackground
