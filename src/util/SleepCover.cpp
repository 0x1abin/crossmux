#include "SleepCover.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace sleepCover {
namespace {
struct Paths {
  const char* image;
  const char* part;
  const char* backup;
  const char* transaction;
  bool overlay;
};
constexpr Paths normalPaths{imagePath, partPath, backupPath, "/sleep.bmp.txn", false};
constexpr Paths overlayPaths{overlayImagePath, overlayPartPath, "/sleep-overlay.bmp.bak", "/sleep-overlay.bmp.txn",
                             true};
constexpr uint8_t transactionVersion = 1;

bool validImage(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  Bitmap bitmap(file, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  return file.position() + static_cast<uint64_t>(bitmap.getRowBytes()) * bitmap.getHeight() <= file.fileSize64();
}

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

bool restore(const Paths& paths, uint8_t previousMode, bool hadPrevious) {
  if (Storage.exists(paths.backup)) {
    if (!validImage(paths.backup) || !removeIfPresent(paths.image) || !Storage.rename(paths.backup, paths.image))
      return false;
  } else if (hadPrevious) {
    if (!Storage.exists(paths.image)) return false;
  } else if (!removeIfPresent(paths.image)) {
    return false;
  }
  SETTINGS.sleepScreen = previousMode;
  if (!SETTINGS.saveToFile()) return false;
  return removeIfPresent(paths.part) && removeIfPresent(paths.transaction);
}
}  // namespace

bool recoverPaths(const Paths& paths) {
  if (Storage.exists(paths.transaction)) {
    uint8_t state[3];
    {
      HalFile file;
      if (!Storage.openFileForRead("SLP", paths.transaction, file) || file.fileSize64() != sizeof(state) ||
          file.read(state, sizeof(state)) != sizeof(state) || state[0] != transactionVersion ||
          state[1] >= CrossPointSettings::SLEEP_SCREEN_MODE_COUNT || state[2] > 1) {
        LOG_ERR("SLP", "Invalid sleep-cover transaction; preserving recovery files");
        return false;
      }
    }
    if (!restore(paths, state[1], state[2] != 0)) {
      LOG_ERR("SLP", "Could not restore sleep cover; preserving transaction and backup");
      return false;
    }
    return true;
  }
  if (!removeIfPresent(paths.part)) return false;
  if (!Storage.exists(paths.backup)) return true;
  const bool custom = paths.overlay ? SETTINGS.sleepScreen == CrossPointSettings::COVER_TRANSPARENT
                                    : SETTINGS.sleepScreen == CrossPointSettings::CUSTOM ||
                                          SETTINGS.sleepScreen == CrossPointSettings::TRANSPARENT;
  if (custom && validImage(paths.image)) {
    // Legacy .bak or a commit interrupted after removing its transaction marker.
    return SETTINGS.saveToFile() && removeIfPresent(paths.backup);
  }
  if (!validImage(paths.backup) || !removeIfPresent(paths.image) || !Storage.rename(paths.backup, paths.image)) {
    LOG_ERR("SLP", "Could not recover previous sleep cover");
    return false;
  }
  return true;
}

bool recover() { return recoverPaths(normalPaths) && recoverPaths(overlayPaths); }

bool commit(const uint8_t mode) {
  const auto& paths = mode == CrossPointSettings::COVER_TRANSPARENT ? overlayPaths : normalPaths;
  if ((mode != CrossPointSettings::CUSTOM && mode != CrossPointSettings::TRANSPARENT &&
       mode != CrossPointSettings::COVER_TRANSPARENT) ||
      Storage.exists(normalPaths.transaction) || Storage.exists(normalPaths.backup) ||
      Storage.exists(overlayPaths.transaction) || Storage.exists(overlayPaths.backup) || !validImage(paths.part))
    return false;
  const uint8_t previousMode = SETTINGS.sleepScreen;
  const bool hadPrevious = Storage.exists(paths.image);
  const uint8_t state[] = {transactionVersion, previousMode, static_cast<uint8_t>(hadPrevious)};
  bool saved = false;
  {
    HalFile file;
    saved =
        Storage.openFileForWrite("SLP", paths.transaction, file) && file.write(state, sizeof(state)) == sizeof(state);
    if (saved) {
      file.flush();
      saved = file.close();
    }
  }
  if (!saved) {
    removeIfPresent(paths.transaction);
    return false;
  }
  if ((hadPrevious && !Storage.rename(paths.image, paths.backup)) || !Storage.rename(paths.part, paths.image) ||
      !validImage(paths.image)) {
    if (!restore(paths, previousMode, hadPrevious))
      LOG_ERR("SLP", "Sleep-cover rollback incomplete; preserving recovery files");
    return false;
  }
  SETTINGS.sleepScreen = mode;
  if (!SETTINGS.saveToFile() || !Storage.remove(paths.transaction)) {
    if (!restore(paths, previousMode, hadPrevious))
      LOG_ERR("SLP", "Sleep-cover rollback incomplete; preserving recovery files");
    return false;
  }
  // The image and settings are durable; a failed backup removal can be retried by recover().
  if (!removeIfPresent(paths.backup)) LOG_ERR("SLP", "Sleep-cover committed; backup cleanup pending");
  return true;
}
}  // namespace sleepCover
